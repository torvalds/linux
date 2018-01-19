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
#include <linux/netdevice.h>
#include <linux/completion.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/random.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <asm/byteorder.h>
#include <net/devlink.h>
#include <trace/events/devlink.h>

#include "core.h"
#include "item.h"
#include "cmd.h"
#include "port.h"
#include "trap.h"
#include "emad.h"
#include "reg.h"
#include "resources.h"

static LIST_HEAD(mlxsw_core_driver_list);
static DEFINE_SPINLOCK(mlxsw_core_driver_list_lock);

static const char mlxsw_core_driver_name[] = "mlxsw_core";

static struct workqueue_struct *mlxsw_wq;
static struct workqueue_struct *mlxsw_owq;

struct mlxsw_core_port {
	struct devlink_port devlink_port;
	void *port_driver_priv;
	u8 local_port;
};

void *mlxsw_core_port_driver_priv(struct mlxsw_core_port *mlxsw_core_port)
{
	return mlxsw_core_port->port_driver_priv;
}
EXPORT_SYMBOL(mlxsw_core_port_driver_priv);

static bool mlxsw_core_port_check(struct mlxsw_core_port *mlxsw_core_port)
{
	return mlxsw_core_port->port_driver_priv != NULL;
}

struct mlxsw_core {
	struct mlxsw_driver *driver;
	const struct mlxsw_bus *bus;
	void *bus_priv;
	const struct mlxsw_bus_info *bus_info;
	struct workqueue_struct *emad_wq;
	struct list_head rx_listener_list;
	struct list_head event_listener_list;
	struct {
		atomic64_t tid;
		struct list_head trans_list;
		spinlock_t trans_list_lock; /* protects trans_list writes */
		bool use_emad;
	} emad;
	struct {
		u8 *mapping; /* lag_id+port_index to local_port mapping */
	} lag;
	struct mlxsw_res res;
	struct mlxsw_hwmon *hwmon;
	struct mlxsw_thermal *thermal;
	struct mlxsw_core_port *ports;
	unsigned int max_ports;
	unsigned long driver_priv[0];
	/* driver_priv has to be always the last item */
};

#define MLXSW_PORT_MAX_PORTS_DEFAULT	0x40

static int mlxsw_ports_init(struct mlxsw_core *mlxsw_core)
{
	/* Switch ports are numbered from 1 to queried value */
	if (MLXSW_CORE_RES_VALID(mlxsw_core, MAX_SYSTEM_PORT))
		mlxsw_core->max_ports = MLXSW_CORE_RES_GET(mlxsw_core,
							   MAX_SYSTEM_PORT) + 1;
	else
		mlxsw_core->max_ports = MLXSW_PORT_MAX_PORTS_DEFAULT + 1;

	mlxsw_core->ports = kcalloc(mlxsw_core->max_ports,
				    sizeof(struct mlxsw_core_port), GFP_KERNEL);
	if (!mlxsw_core->ports)
		return -ENOMEM;

	return 0;
}

static void mlxsw_ports_fini(struct mlxsw_core *mlxsw_core)
{
	kfree(mlxsw_core->ports);
}

unsigned int mlxsw_core_max_ports(const struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->max_ports;
}
EXPORT_SYMBOL(mlxsw_core_max_ports);

void *mlxsw_core_driver_priv(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->driver_priv;
}
EXPORT_SYMBOL(mlxsw_core_driver_priv);

struct mlxsw_rx_listener_item {
	struct list_head list;
	struct mlxsw_rx_listener rxl;
	void *priv;
};

struct mlxsw_event_listener_item {
	struct list_head list;
	struct mlxsw_event_listener el;
	void *priv;
};

/******************
 * EMAD processing
 ******************/

/* emad_eth_hdr_dmac
 * Destination MAC in EMAD's Ethernet header.
 * Must be set to 01:02:c9:00:00:01
 */
MLXSW_ITEM_BUF(emad, eth_hdr, dmac, 0x00, 6);

/* emad_eth_hdr_smac
 * Source MAC in EMAD's Ethernet header.
 * Must be set to 00:02:c9:01:02:03
 */
MLXSW_ITEM_BUF(emad, eth_hdr, smac, 0x06, 6);

/* emad_eth_hdr_ethertype
 * Ethertype in EMAD's Ethernet header.
 * Must be set to 0x8932
 */
MLXSW_ITEM32(emad, eth_hdr, ethertype, 0x0C, 16, 16);

/* emad_eth_hdr_mlx_proto
 * Mellanox protocol.
 * Must be set to 0x0.
 */
MLXSW_ITEM32(emad, eth_hdr, mlx_proto, 0x0C, 8, 8);

/* emad_eth_hdr_ver
 * Mellanox protocol version.
 * Must be set to 0x0.
 */
MLXSW_ITEM32(emad, eth_hdr, ver, 0x0C, 4, 4);

/* emad_op_tlv_type
 * Type of the TLV.
 * Must be set to 0x1 (operation TLV).
 */
MLXSW_ITEM32(emad, op_tlv, type, 0x00, 27, 5);

/* emad_op_tlv_len
 * Length of the operation TLV in u32.
 * Must be set to 0x4.
 */
MLXSW_ITEM32(emad, op_tlv, len, 0x00, 16, 11);

/* emad_op_tlv_dr
 * Direct route bit. Setting to 1 indicates the EMAD is a direct route
 * EMAD. DR TLV must follow.
 *
 * Note: Currently not supported and must not be set.
 */
MLXSW_ITEM32(emad, op_tlv, dr, 0x00, 15, 1);

/* emad_op_tlv_status
 * Returned status in case of EMAD response. Must be set to 0 in case
 * of EMAD request.
 * 0x0 - success
 * 0x1 - device is busy. Requester should retry
 * 0x2 - Mellanox protocol version not supported
 * 0x3 - unknown TLV
 * 0x4 - register not supported
 * 0x5 - operation class not supported
 * 0x6 - EMAD method not supported
 * 0x7 - bad parameter (e.g. port out of range)
 * 0x8 - resource not available
 * 0x9 - message receipt acknowledgment. Requester should retry
 * 0x70 - internal error
 */
MLXSW_ITEM32(emad, op_tlv, status, 0x00, 8, 7);

/* emad_op_tlv_register_id
 * Register ID of register within register TLV.
 */
MLXSW_ITEM32(emad, op_tlv, register_id, 0x04, 16, 16);

/* emad_op_tlv_r
 * Response bit. Setting to 1 indicates Response, otherwise request.
 */
MLXSW_ITEM32(emad, op_tlv, r, 0x04, 15, 1);

/* emad_op_tlv_method
 * EMAD method type.
 * 0x1 - query
 * 0x2 - write
 * 0x3 - send (currently not supported)
 * 0x4 - event
 */
MLXSW_ITEM32(emad, op_tlv, method, 0x04, 8, 7);

/* emad_op_tlv_class
 * EMAD operation class. Must be set to 0x1 (REG_ACCESS).
 */
MLXSW_ITEM32(emad, op_tlv, class, 0x04, 0, 8);

/* emad_op_tlv_tid
 * EMAD transaction ID. Used for pairing request and response EMADs.
 */
MLXSW_ITEM64(emad, op_tlv, tid, 0x08, 0, 64);

/* emad_reg_tlv_type
 * Type of the TLV.
 * Must be set to 0x3 (register TLV).
 */
MLXSW_ITEM32(emad, reg_tlv, type, 0x00, 27, 5);

/* emad_reg_tlv_len
 * Length of the operation TLV in u32.
 */
MLXSW_ITEM32(emad, reg_tlv, len, 0x00, 16, 11);

/* emad_end_tlv_type
 * Type of the TLV.
 * Must be set to 0x0 (end TLV).
 */
MLXSW_ITEM32(emad, end_tlv, type, 0x00, 27, 5);

/* emad_end_tlv_len
 * Length of the end TLV in u32.
 * Must be set to 1.
 */
MLXSW_ITEM32(emad, end_tlv, len, 0x00, 16, 11);

enum mlxsw_core_reg_access_type {
	MLXSW_CORE_REG_ACCESS_TYPE_QUERY,
	MLXSW_CORE_REG_ACCESS_TYPE_WRITE,
};

static inline const char *
mlxsw_core_reg_access_type_str(enum mlxsw_core_reg_access_type type)
{
	switch (type) {
	case MLXSW_CORE_REG_ACCESS_TYPE_QUERY:
		return "query";
	case MLXSW_CORE_REG_ACCESS_TYPE_WRITE:
		return "write";
	}
	BUG();
}

static void mlxsw_emad_pack_end_tlv(char *end_tlv)
{
	mlxsw_emad_end_tlv_type_set(end_tlv, MLXSW_EMAD_TLV_TYPE_END);
	mlxsw_emad_end_tlv_len_set(end_tlv, MLXSW_EMAD_END_TLV_LEN);
}

static void mlxsw_emad_pack_reg_tlv(char *reg_tlv,
				    const struct mlxsw_reg_info *reg,
				    char *payload)
{
	mlxsw_emad_reg_tlv_type_set(reg_tlv, MLXSW_EMAD_TLV_TYPE_REG);
	mlxsw_emad_reg_tlv_len_set(reg_tlv, reg->len / sizeof(u32) + 1);
	memcpy(reg_tlv + sizeof(u32), payload, reg->len);
}

static void mlxsw_emad_pack_op_tlv(char *op_tlv,
				   const struct mlxsw_reg_info *reg,
				   enum mlxsw_core_reg_access_type type,
				   u64 tid)
{
	mlxsw_emad_op_tlv_type_set(op_tlv, MLXSW_EMAD_TLV_TYPE_OP);
	mlxsw_emad_op_tlv_len_set(op_tlv, MLXSW_EMAD_OP_TLV_LEN);
	mlxsw_emad_op_tlv_dr_set(op_tlv, 0);
	mlxsw_emad_op_tlv_status_set(op_tlv, 0);
	mlxsw_emad_op_tlv_register_id_set(op_tlv, reg->id);
	mlxsw_emad_op_tlv_r_set(op_tlv, MLXSW_EMAD_OP_TLV_REQUEST);
	if (type == MLXSW_CORE_REG_ACCESS_TYPE_QUERY)
		mlxsw_emad_op_tlv_method_set(op_tlv,
					     MLXSW_EMAD_OP_TLV_METHOD_QUERY);
	else
		mlxsw_emad_op_tlv_method_set(op_tlv,
					     MLXSW_EMAD_OP_TLV_METHOD_WRITE);
	mlxsw_emad_op_tlv_class_set(op_tlv,
				    MLXSW_EMAD_OP_TLV_CLASS_REG_ACCESS);
	mlxsw_emad_op_tlv_tid_set(op_tlv, tid);
}

static int mlxsw_emad_construct_eth_hdr(struct sk_buff *skb)
{
	char *eth_hdr = skb_push(skb, MLXSW_EMAD_ETH_HDR_LEN);

	mlxsw_emad_eth_hdr_dmac_memcpy_to(eth_hdr, MLXSW_EMAD_EH_DMAC);
	mlxsw_emad_eth_hdr_smac_memcpy_to(eth_hdr, MLXSW_EMAD_EH_SMAC);
	mlxsw_emad_eth_hdr_ethertype_set(eth_hdr, MLXSW_EMAD_EH_ETHERTYPE);
	mlxsw_emad_eth_hdr_mlx_proto_set(eth_hdr, MLXSW_EMAD_EH_MLX_PROTO);
	mlxsw_emad_eth_hdr_ver_set(eth_hdr, MLXSW_EMAD_EH_PROTO_VERSION);

	skb_reset_mac_header(skb);

	return 0;
}

static void mlxsw_emad_construct(struct sk_buff *skb,
				 const struct mlxsw_reg_info *reg,
				 char *payload,
				 enum mlxsw_core_reg_access_type type,
				 u64 tid)
{
	char *buf;

	buf = skb_push(skb, MLXSW_EMAD_END_TLV_LEN * sizeof(u32));
	mlxsw_emad_pack_end_tlv(buf);

	buf = skb_push(skb, reg->len + sizeof(u32));
	mlxsw_emad_pack_reg_tlv(buf, reg, payload);

	buf = skb_push(skb, MLXSW_EMAD_OP_TLV_LEN * sizeof(u32));
	mlxsw_emad_pack_op_tlv(buf, reg, type, tid);

	mlxsw_emad_construct_eth_hdr(skb);
}

static char *mlxsw_emad_op_tlv(const struct sk_buff *skb)
{
	return ((char *) (skb->data + MLXSW_EMAD_ETH_HDR_LEN));
}

static char *mlxsw_emad_reg_tlv(const struct sk_buff *skb)
{
	return ((char *) (skb->data + MLXSW_EMAD_ETH_HDR_LEN +
				      MLXSW_EMAD_OP_TLV_LEN * sizeof(u32)));
}

static char *mlxsw_emad_reg_payload(const char *op_tlv)
{
	return ((char *) (op_tlv + (MLXSW_EMAD_OP_TLV_LEN + 1) * sizeof(u32)));
}

static u64 mlxsw_emad_get_tid(const struct sk_buff *skb)
{
	char *op_tlv;

	op_tlv = mlxsw_emad_op_tlv(skb);
	return mlxsw_emad_op_tlv_tid_get(op_tlv);
}

static bool mlxsw_emad_is_resp(const struct sk_buff *skb)
{
	char *op_tlv;

	op_tlv = mlxsw_emad_op_tlv(skb);
	return (mlxsw_emad_op_tlv_r_get(op_tlv) == MLXSW_EMAD_OP_TLV_RESPONSE);
}

static int mlxsw_emad_process_status(char *op_tlv,
				     enum mlxsw_emad_op_tlv_status *p_status)
{
	*p_status = mlxsw_emad_op_tlv_status_get(op_tlv);

	switch (*p_status) {
	case MLXSW_EMAD_OP_TLV_STATUS_SUCCESS:
		return 0;
	case MLXSW_EMAD_OP_TLV_STATUS_BUSY:
	case MLXSW_EMAD_OP_TLV_STATUS_MESSAGE_RECEIPT_ACK:
		return -EAGAIN;
	case MLXSW_EMAD_OP_TLV_STATUS_VERSION_NOT_SUPPORTED:
	case MLXSW_EMAD_OP_TLV_STATUS_UNKNOWN_TLV:
	case MLXSW_EMAD_OP_TLV_STATUS_REGISTER_NOT_SUPPORTED:
	case MLXSW_EMAD_OP_TLV_STATUS_CLASS_NOT_SUPPORTED:
	case MLXSW_EMAD_OP_TLV_STATUS_METHOD_NOT_SUPPORTED:
	case MLXSW_EMAD_OP_TLV_STATUS_BAD_PARAMETER:
	case MLXSW_EMAD_OP_TLV_STATUS_RESOURCE_NOT_AVAILABLE:
	case MLXSW_EMAD_OP_TLV_STATUS_INTERNAL_ERROR:
	default:
		return -EIO;
	}
}

static int
mlxsw_emad_process_status_skb(struct sk_buff *skb,
			      enum mlxsw_emad_op_tlv_status *p_status)
{
	return mlxsw_emad_process_status(mlxsw_emad_op_tlv(skb), p_status);
}

struct mlxsw_reg_trans {
	struct list_head list;
	struct list_head bulk_list;
	struct mlxsw_core *core;
	struct sk_buff *tx_skb;
	struct mlxsw_tx_info tx_info;
	struct delayed_work timeout_dw;
	unsigned int retries;
	u64 tid;
	struct completion completion;
	atomic_t active;
	mlxsw_reg_trans_cb_t *cb;
	unsigned long cb_priv;
	const struct mlxsw_reg_info *reg;
	enum mlxsw_core_reg_access_type type;
	int err;
	enum mlxsw_emad_op_tlv_status emad_status;
	struct rcu_head rcu;
};

#define MLXSW_EMAD_TIMEOUT_MS 200

static void mlxsw_emad_trans_timeout_schedule(struct mlxsw_reg_trans *trans)
{
	unsigned long timeout = msecs_to_jiffies(MLXSW_EMAD_TIMEOUT_MS);

	queue_delayed_work(trans->core->emad_wq, &trans->timeout_dw, timeout);
}

static int mlxsw_emad_transmit(struct mlxsw_core *mlxsw_core,
			       struct mlxsw_reg_trans *trans)
{
	struct sk_buff *skb;
	int err;

	skb = skb_copy(trans->tx_skb, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	trace_devlink_hwmsg(priv_to_devlink(mlxsw_core), false, 0,
			    skb->data + mlxsw_core->driver->txhdr_len,
			    skb->len - mlxsw_core->driver->txhdr_len);

	atomic_set(&trans->active, 1);
	err = mlxsw_core_skb_transmit(mlxsw_core, skb, &trans->tx_info);
	if (err) {
		dev_kfree_skb(skb);
		return err;
	}
	mlxsw_emad_trans_timeout_schedule(trans);
	return 0;
}

static void mlxsw_emad_trans_finish(struct mlxsw_reg_trans *trans, int err)
{
	struct mlxsw_core *mlxsw_core = trans->core;

	dev_kfree_skb(trans->tx_skb);
	spin_lock_bh(&mlxsw_core->emad.trans_list_lock);
	list_del_rcu(&trans->list);
	spin_unlock_bh(&mlxsw_core->emad.trans_list_lock);
	trans->err = err;
	complete(&trans->completion);
}

static void mlxsw_emad_transmit_retry(struct mlxsw_core *mlxsw_core,
				      struct mlxsw_reg_trans *trans)
{
	int err;

	if (trans->retries < MLXSW_EMAD_MAX_RETRY) {
		trans->retries++;
		err = mlxsw_emad_transmit(trans->core, trans);
		if (err == 0)
			return;
	} else {
		err = -EIO;
	}
	mlxsw_emad_trans_finish(trans, err);
}

static void mlxsw_emad_trans_timeout_work(struct work_struct *work)
{
	struct mlxsw_reg_trans *trans = container_of(work,
						     struct mlxsw_reg_trans,
						     timeout_dw.work);

	if (!atomic_dec_and_test(&trans->active))
		return;

	mlxsw_emad_transmit_retry(trans->core, trans);
}

static void mlxsw_emad_process_response(struct mlxsw_core *mlxsw_core,
					struct mlxsw_reg_trans *trans,
					struct sk_buff *skb)
{
	int err;

	if (!atomic_dec_and_test(&trans->active))
		return;

	err = mlxsw_emad_process_status_skb(skb, &trans->emad_status);
	if (err == -EAGAIN) {
		mlxsw_emad_transmit_retry(mlxsw_core, trans);
	} else {
		if (err == 0) {
			char *op_tlv = mlxsw_emad_op_tlv(skb);

			if (trans->cb)
				trans->cb(mlxsw_core,
					  mlxsw_emad_reg_payload(op_tlv),
					  trans->reg->len, trans->cb_priv);
		}
		mlxsw_emad_trans_finish(trans, err);
	}
}

/* called with rcu read lock held */
static void mlxsw_emad_rx_listener_func(struct sk_buff *skb, u8 local_port,
					void *priv)
{
	struct mlxsw_core *mlxsw_core = priv;
	struct mlxsw_reg_trans *trans;

	trace_devlink_hwmsg(priv_to_devlink(mlxsw_core), true, 0,
			    skb->data, skb->len);

	if (!mlxsw_emad_is_resp(skb))
		goto free_skb;

	list_for_each_entry_rcu(trans, &mlxsw_core->emad.trans_list, list) {
		if (mlxsw_emad_get_tid(skb) == trans->tid) {
			mlxsw_emad_process_response(mlxsw_core, trans, skb);
			break;
		}
	}

free_skb:
	dev_kfree_skb(skb);
}

static const struct mlxsw_listener mlxsw_emad_rx_listener =
	MLXSW_RXL(mlxsw_emad_rx_listener_func, ETHEMAD, TRAP_TO_CPU, false,
		  EMAD, DISCARD);

static int mlxsw_emad_init(struct mlxsw_core *mlxsw_core)
{
	struct workqueue_struct *emad_wq;
	u64 tid;
	int err;

	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_TXRX))
		return 0;

	emad_wq = alloc_workqueue("mlxsw_core_emad", WQ_MEM_RECLAIM, 0);
	if (!emad_wq)
		return -ENOMEM;
	mlxsw_core->emad_wq = emad_wq;

	/* Set the upper 32 bits of the transaction ID field to a random
	 * number. This allows us to discard EMADs addressed to other
	 * devices.
	 */
	get_random_bytes(&tid, 4);
	tid <<= 32;
	atomic64_set(&mlxsw_core->emad.tid, tid);

	INIT_LIST_HEAD(&mlxsw_core->emad.trans_list);
	spin_lock_init(&mlxsw_core->emad.trans_list_lock);

	err = mlxsw_core_trap_register(mlxsw_core, &mlxsw_emad_rx_listener,
				       mlxsw_core);
	if (err)
		return err;

	err = mlxsw_core->driver->basic_trap_groups_set(mlxsw_core);
	if (err)
		goto err_emad_trap_set;
	mlxsw_core->emad.use_emad = true;

	return 0;

err_emad_trap_set:
	mlxsw_core_trap_unregister(mlxsw_core, &mlxsw_emad_rx_listener,
				   mlxsw_core);
	destroy_workqueue(mlxsw_core->emad_wq);
	return err;
}

static void mlxsw_emad_fini(struct mlxsw_core *mlxsw_core)
{

	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_TXRX))
		return;

	mlxsw_core->emad.use_emad = false;
	mlxsw_core_trap_unregister(mlxsw_core, &mlxsw_emad_rx_listener,
				   mlxsw_core);
	destroy_workqueue(mlxsw_core->emad_wq);
}

static struct sk_buff *mlxsw_emad_alloc(const struct mlxsw_core *mlxsw_core,
					u16 reg_len)
{
	struct sk_buff *skb;
	u16 emad_len;

	emad_len = (reg_len + sizeof(u32) + MLXSW_EMAD_ETH_HDR_LEN +
		    (MLXSW_EMAD_OP_TLV_LEN + MLXSW_EMAD_END_TLV_LEN) *
		    sizeof(u32) + mlxsw_core->driver->txhdr_len);
	if (emad_len > MLXSW_EMAD_MAX_FRAME_LEN)
		return NULL;

	skb = netdev_alloc_skb(NULL, emad_len);
	if (!skb)
		return NULL;
	memset(skb->data, 0, emad_len);
	skb_reserve(skb, emad_len);

	return skb;
}

static int mlxsw_emad_reg_access(struct mlxsw_core *mlxsw_core,
				 const struct mlxsw_reg_info *reg,
				 char *payload,
				 enum mlxsw_core_reg_access_type type,
				 struct mlxsw_reg_trans *trans,
				 struct list_head *bulk_list,
				 mlxsw_reg_trans_cb_t *cb,
				 unsigned long cb_priv, u64 tid)
{
	struct sk_buff *skb;
	int err;

	dev_dbg(mlxsw_core->bus_info->dev, "EMAD reg access (tid=%llx,reg_id=%x(%s),type=%s)\n",
		tid, reg->id, mlxsw_reg_id_str(reg->id),
		mlxsw_core_reg_access_type_str(type));

	skb = mlxsw_emad_alloc(mlxsw_core, reg->len);
	if (!skb)
		return -ENOMEM;

	list_add_tail(&trans->bulk_list, bulk_list);
	trans->core = mlxsw_core;
	trans->tx_skb = skb;
	trans->tx_info.local_port = MLXSW_PORT_CPU_PORT;
	trans->tx_info.is_emad = true;
	INIT_DELAYED_WORK(&trans->timeout_dw, mlxsw_emad_trans_timeout_work);
	trans->tid = tid;
	init_completion(&trans->completion);
	trans->cb = cb;
	trans->cb_priv = cb_priv;
	trans->reg = reg;
	trans->type = type;

	mlxsw_emad_construct(skb, reg, payload, type, trans->tid);
	mlxsw_core->driver->txhdr_construct(skb, &trans->tx_info);

	spin_lock_bh(&mlxsw_core->emad.trans_list_lock);
	list_add_tail_rcu(&trans->list, &mlxsw_core->emad.trans_list);
	spin_unlock_bh(&mlxsw_core->emad.trans_list_lock);
	err = mlxsw_emad_transmit(mlxsw_core, trans);
	if (err)
		goto err_out;
	return 0;

err_out:
	spin_lock_bh(&mlxsw_core->emad.trans_list_lock);
	list_del_rcu(&trans->list);
	spin_unlock_bh(&mlxsw_core->emad.trans_list_lock);
	list_del(&trans->bulk_list);
	dev_kfree_skb(trans->tx_skb);
	return err;
}

/*****************
 * Core functions
 *****************/

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
	spin_unlock(&mlxsw_core_driver_list_lock);
	return mlxsw_driver;
}

static void mlxsw_core_driver_put(const char *kind)
{
	struct mlxsw_driver *mlxsw_driver;

	spin_lock(&mlxsw_core_driver_list_lock);
	mlxsw_driver = __driver_find(kind);
	spin_unlock(&mlxsw_core_driver_list_lock);
}

static int mlxsw_devlink_port_split(struct devlink *devlink,
				    unsigned int port_index,
				    unsigned int count)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	if (port_index >= mlxsw_core->max_ports)
		return -EINVAL;
	if (!mlxsw_core->driver->port_split)
		return -EOPNOTSUPP;
	return mlxsw_core->driver->port_split(mlxsw_core, port_index, count);
}

static int mlxsw_devlink_port_unsplit(struct devlink *devlink,
				      unsigned int port_index)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	if (port_index >= mlxsw_core->max_ports)
		return -EINVAL;
	if (!mlxsw_core->driver->port_unsplit)
		return -EOPNOTSUPP;
	return mlxsw_core->driver->port_unsplit(mlxsw_core, port_index);
}

static int
mlxsw_devlink_sb_pool_get(struct devlink *devlink,
			  unsigned int sb_index, u16 pool_index,
			  struct devlink_sb_pool_info *pool_info)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->sb_pool_get)
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_pool_get(mlxsw_core, sb_index,
					 pool_index, pool_info);
}

static int
mlxsw_devlink_sb_pool_set(struct devlink *devlink,
			  unsigned int sb_index, u16 pool_index, u32 size,
			  enum devlink_sb_threshold_type threshold_type)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->sb_pool_set)
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_pool_set(mlxsw_core, sb_index,
					 pool_index, size, threshold_type);
}

static void *__dl_port(struct devlink_port *devlink_port)
{
	return container_of(devlink_port, struct mlxsw_core_port, devlink_port);
}

static int mlxsw_devlink_port_type_set(struct devlink_port *devlink_port,
				       enum devlink_port_type port_type)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->port_type_set)
		return -EOPNOTSUPP;

	return mlxsw_driver->port_type_set(mlxsw_core,
					   mlxsw_core_port->local_port,
					   port_type);
}

static int mlxsw_devlink_sb_port_pool_get(struct devlink_port *devlink_port,
					  unsigned int sb_index, u16 pool_index,
					  u32 *p_threshold)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_port_pool_get ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_port_pool_get(mlxsw_core_port, sb_index,
					      pool_index, p_threshold);
}

static int mlxsw_devlink_sb_port_pool_set(struct devlink_port *devlink_port,
					  unsigned int sb_index, u16 pool_index,
					  u32 threshold)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_port_pool_set ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_port_pool_set(mlxsw_core_port, sb_index,
					      pool_index, threshold);
}

static int
mlxsw_devlink_sb_tc_pool_bind_get(struct devlink_port *devlink_port,
				  unsigned int sb_index, u16 tc_index,
				  enum devlink_sb_pool_type pool_type,
				  u16 *p_pool_index, u32 *p_threshold)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_tc_pool_bind_get ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_tc_pool_bind_get(mlxsw_core_port, sb_index,
						 tc_index, pool_type,
						 p_pool_index, p_threshold);
}

static int
mlxsw_devlink_sb_tc_pool_bind_set(struct devlink_port *devlink_port,
				  unsigned int sb_index, u16 tc_index,
				  enum devlink_sb_pool_type pool_type,
				  u16 pool_index, u32 threshold)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_tc_pool_bind_set ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_tc_pool_bind_set(mlxsw_core_port, sb_index,
						 tc_index, pool_type,
						 pool_index, threshold);
}

static int mlxsw_devlink_sb_occ_snapshot(struct devlink *devlink,
					 unsigned int sb_index)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->sb_occ_snapshot)
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_occ_snapshot(mlxsw_core, sb_index);
}

static int mlxsw_devlink_sb_occ_max_clear(struct devlink *devlink,
					  unsigned int sb_index)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->sb_occ_max_clear)
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_occ_max_clear(mlxsw_core, sb_index);
}

static int
mlxsw_devlink_sb_occ_port_pool_get(struct devlink_port *devlink_port,
				   unsigned int sb_index, u16 pool_index,
				   u32 *p_cur, u32 *p_max)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_occ_port_pool_get ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_occ_port_pool_get(mlxsw_core_port, sb_index,
						  pool_index, p_cur, p_max);
}

static int
mlxsw_devlink_sb_occ_tc_port_bind_get(struct devlink_port *devlink_port,
				      unsigned int sb_index, u16 tc_index,
				      enum devlink_sb_pool_type pool_type,
				      u32 *p_cur, u32 *p_max)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_occ_tc_port_bind_get ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_occ_tc_port_bind_get(mlxsw_core_port,
						     sb_index, tc_index,
						     pool_type, p_cur, p_max);
}

static const struct devlink_ops mlxsw_devlink_ops = {
	.port_type_set			= mlxsw_devlink_port_type_set,
	.port_split			= mlxsw_devlink_port_split,
	.port_unsplit			= mlxsw_devlink_port_unsplit,
	.sb_pool_get			= mlxsw_devlink_sb_pool_get,
	.sb_pool_set			= mlxsw_devlink_sb_pool_set,
	.sb_port_pool_get		= mlxsw_devlink_sb_port_pool_get,
	.sb_port_pool_set		= mlxsw_devlink_sb_port_pool_set,
	.sb_tc_pool_bind_get		= mlxsw_devlink_sb_tc_pool_bind_get,
	.sb_tc_pool_bind_set		= mlxsw_devlink_sb_tc_pool_bind_set,
	.sb_occ_snapshot		= mlxsw_devlink_sb_occ_snapshot,
	.sb_occ_max_clear		= mlxsw_devlink_sb_occ_max_clear,
	.sb_occ_port_pool_get		= mlxsw_devlink_sb_occ_port_pool_get,
	.sb_occ_tc_port_bind_get	= mlxsw_devlink_sb_occ_tc_port_bind_get,
};

int mlxsw_core_bus_device_register(const struct mlxsw_bus_info *mlxsw_bus_info,
				   const struct mlxsw_bus *mlxsw_bus,
				   void *bus_priv)
{
	const char *device_kind = mlxsw_bus_info->device_kind;
	struct mlxsw_core *mlxsw_core;
	struct mlxsw_driver *mlxsw_driver;
	struct devlink *devlink;
	size_t alloc_size;
	int err;

	mlxsw_driver = mlxsw_core_driver_get(device_kind);
	if (!mlxsw_driver)
		return -EINVAL;
	alloc_size = sizeof(*mlxsw_core) + mlxsw_driver->priv_size;
	devlink = devlink_alloc(&mlxsw_devlink_ops, alloc_size);
	if (!devlink) {
		err = -ENOMEM;
		goto err_devlink_alloc;
	}

	mlxsw_core = devlink_priv(devlink);
	INIT_LIST_HEAD(&mlxsw_core->rx_listener_list);
	INIT_LIST_HEAD(&mlxsw_core->event_listener_list);
	mlxsw_core->driver = mlxsw_driver;
	mlxsw_core->bus = mlxsw_bus;
	mlxsw_core->bus_priv = bus_priv;
	mlxsw_core->bus_info = mlxsw_bus_info;

	err = mlxsw_bus->init(bus_priv, mlxsw_core, mlxsw_driver->profile,
			      &mlxsw_core->res);
	if (err)
		goto err_bus_init;

	err = mlxsw_ports_init(mlxsw_core);
	if (err)
		goto err_ports_init;

	if (MLXSW_CORE_RES_VALID(mlxsw_core, MAX_LAG) &&
	    MLXSW_CORE_RES_VALID(mlxsw_core, MAX_LAG_MEMBERS)) {
		alloc_size = sizeof(u8) *
			MLXSW_CORE_RES_GET(mlxsw_core, MAX_LAG) *
			MLXSW_CORE_RES_GET(mlxsw_core, MAX_LAG_MEMBERS);
		mlxsw_core->lag.mapping = kzalloc(alloc_size, GFP_KERNEL);
		if (!mlxsw_core->lag.mapping) {
			err = -ENOMEM;
			goto err_alloc_lag_mapping;
		}
	}

	err = mlxsw_emad_init(mlxsw_core);
	if (err)
		goto err_emad_init;

	err = devlink_register(devlink, mlxsw_bus_info->dev);
	if (err)
		goto err_devlink_register;

	err = mlxsw_hwmon_init(mlxsw_core, mlxsw_bus_info, &mlxsw_core->hwmon);
	if (err)
		goto err_hwmon_init;

	err = mlxsw_thermal_init(mlxsw_core, mlxsw_bus_info,
				 &mlxsw_core->thermal);
	if (err)
		goto err_thermal_init;

	if (mlxsw_driver->init) {
		err = mlxsw_driver->init(mlxsw_core, mlxsw_bus_info);
		if (err)
			goto err_driver_init;
	}

	return 0;

err_driver_init:
	mlxsw_thermal_fini(mlxsw_core->thermal);
err_thermal_init:
err_hwmon_init:
	devlink_unregister(devlink);
err_devlink_register:
	mlxsw_emad_fini(mlxsw_core);
err_emad_init:
	kfree(mlxsw_core->lag.mapping);
err_alloc_lag_mapping:
	mlxsw_ports_fini(mlxsw_core);
err_ports_init:
	mlxsw_bus->fini(bus_priv);
err_bus_init:
	devlink_free(devlink);
err_devlink_alloc:
	mlxsw_core_driver_put(device_kind);
	return err;
}
EXPORT_SYMBOL(mlxsw_core_bus_device_register);

void mlxsw_core_bus_device_unregister(struct mlxsw_core *mlxsw_core)
{
	const char *device_kind = mlxsw_core->bus_info->device_kind;
	struct devlink *devlink = priv_to_devlink(mlxsw_core);

	if (mlxsw_core->driver->fini)
		mlxsw_core->driver->fini(mlxsw_core);
	mlxsw_thermal_fini(mlxsw_core->thermal);
	devlink_unregister(devlink);
	mlxsw_emad_fini(mlxsw_core);
	kfree(mlxsw_core->lag.mapping);
	mlxsw_ports_fini(mlxsw_core);
	mlxsw_core->bus->fini(mlxsw_core->bus_priv);
	devlink_free(devlink);
	mlxsw_core_driver_put(device_kind);
}
EXPORT_SYMBOL(mlxsw_core_bus_device_unregister);

bool mlxsw_core_skb_transmit_busy(struct mlxsw_core *mlxsw_core,
				  const struct mlxsw_tx_info *tx_info)
{
	return mlxsw_core->bus->skb_transmit_busy(mlxsw_core->bus_priv,
						  tx_info);
}
EXPORT_SYMBOL(mlxsw_core_skb_transmit_busy);

int mlxsw_core_skb_transmit(struct mlxsw_core *mlxsw_core, struct sk_buff *skb,
			    const struct mlxsw_tx_info *tx_info)
{
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

static void mlxsw_core_event_listener_func(struct sk_buff *skb, u8 local_port,
					   void *priv)
{
	struct mlxsw_event_listener_item *event_listener_item = priv;
	struct mlxsw_reg_info reg;
	char *payload;
	char *op_tlv = mlxsw_emad_op_tlv(skb);
	char *reg_tlv = mlxsw_emad_reg_tlv(skb);

	reg.id = mlxsw_emad_op_tlv_register_id_get(op_tlv);
	reg.len = (mlxsw_emad_reg_tlv_len_get(reg_tlv) - 1) * sizeof(u32);
	payload = mlxsw_emad_reg_payload(op_tlv);
	event_listener_item->el.func(&reg, payload, event_listener_item->priv);
	dev_kfree_skb(skb);
}

static bool __is_event_listener_equal(const struct mlxsw_event_listener *el_a,
				      const struct mlxsw_event_listener *el_b)
{
	return (el_a->func == el_b->func &&
		el_a->trap_id == el_b->trap_id);
}

static struct mlxsw_event_listener_item *
__find_event_listener_item(struct mlxsw_core *mlxsw_core,
			   const struct mlxsw_event_listener *el,
			   void *priv)
{
	struct mlxsw_event_listener_item *el_item;

	list_for_each_entry(el_item, &mlxsw_core->event_listener_list, list) {
		if (__is_event_listener_equal(&el_item->el, el) &&
		    el_item->priv == priv)
			return el_item;
	}
	return NULL;
}

int mlxsw_core_event_listener_register(struct mlxsw_core *mlxsw_core,
				       const struct mlxsw_event_listener *el,
				       void *priv)
{
	int err;
	struct mlxsw_event_listener_item *el_item;
	const struct mlxsw_rx_listener rxl = {
		.func = mlxsw_core_event_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = el->trap_id,
	};

	el_item = __find_event_listener_item(mlxsw_core, el, priv);
	if (el_item)
		return -EEXIST;
	el_item = kmalloc(sizeof(*el_item), GFP_KERNEL);
	if (!el_item)
		return -ENOMEM;
	el_item->el = *el;
	el_item->priv = priv;

	err = mlxsw_core_rx_listener_register(mlxsw_core, &rxl, el_item);
	if (err)
		goto err_rx_listener_register;

	/* No reason to save item if we did not manage to register an RX
	 * listener for it.
	 */
	list_add_rcu(&el_item->list, &mlxsw_core->event_listener_list);

	return 0;

err_rx_listener_register:
	kfree(el_item);
	return err;
}
EXPORT_SYMBOL(mlxsw_core_event_listener_register);

void mlxsw_core_event_listener_unregister(struct mlxsw_core *mlxsw_core,
					  const struct mlxsw_event_listener *el,
					  void *priv)
{
	struct mlxsw_event_listener_item *el_item;
	const struct mlxsw_rx_listener rxl = {
		.func = mlxsw_core_event_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = el->trap_id,
	};

	el_item = __find_event_listener_item(mlxsw_core, el, priv);
	if (!el_item)
		return;
	mlxsw_core_rx_listener_unregister(mlxsw_core, &rxl, el_item);
	list_del(&el_item->list);
	kfree(el_item);
}
EXPORT_SYMBOL(mlxsw_core_event_listener_unregister);

static int mlxsw_core_listener_register(struct mlxsw_core *mlxsw_core,
					const struct mlxsw_listener *listener,
					void *priv)
{
	if (listener->is_event)
		return mlxsw_core_event_listener_register(mlxsw_core,
						&listener->u.event_listener,
						priv);
	else
		return mlxsw_core_rx_listener_register(mlxsw_core,
						&listener->u.rx_listener,
						priv);
}

static void mlxsw_core_listener_unregister(struct mlxsw_core *mlxsw_core,
				      const struct mlxsw_listener *listener,
				      void *priv)
{
	if (listener->is_event)
		mlxsw_core_event_listener_unregister(mlxsw_core,
						     &listener->u.event_listener,
						     priv);
	else
		mlxsw_core_rx_listener_unregister(mlxsw_core,
						  &listener->u.rx_listener,
						  priv);
}

int mlxsw_core_trap_register(struct mlxsw_core *mlxsw_core,
			     const struct mlxsw_listener *listener, void *priv)
{
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int err;

	err = mlxsw_core_listener_register(mlxsw_core, listener, priv);
	if (err)
		return err;

	mlxsw_reg_hpkt_pack(hpkt_pl, listener->action, listener->trap_id,
			    listener->trap_group, listener->is_ctrl);
	err = mlxsw_reg_write(mlxsw_core,  MLXSW_REG(hpkt), hpkt_pl);
	if (err)
		goto err_trap_set;

	return 0;

err_trap_set:
	mlxsw_core_listener_unregister(mlxsw_core, listener, priv);
	return err;
}
EXPORT_SYMBOL(mlxsw_core_trap_register);

void mlxsw_core_trap_unregister(struct mlxsw_core *mlxsw_core,
				const struct mlxsw_listener *listener,
				void *priv)
{
	char hpkt_pl[MLXSW_REG_HPKT_LEN];

	if (!listener->is_event) {
		mlxsw_reg_hpkt_pack(hpkt_pl, listener->unreg_action,
				    listener->trap_id, listener->trap_group,
				    listener->is_ctrl);
		mlxsw_reg_write(mlxsw_core, MLXSW_REG(hpkt), hpkt_pl);
	}

	mlxsw_core_listener_unregister(mlxsw_core, listener, priv);
}
EXPORT_SYMBOL(mlxsw_core_trap_unregister);

static u64 mlxsw_core_tid_get(struct mlxsw_core *mlxsw_core)
{
	return atomic64_inc_return(&mlxsw_core->emad.tid);
}

static int mlxsw_core_reg_access_emad(struct mlxsw_core *mlxsw_core,
				      const struct mlxsw_reg_info *reg,
				      char *payload,
				      enum mlxsw_core_reg_access_type type,
				      struct list_head *bulk_list,
				      mlxsw_reg_trans_cb_t *cb,
				      unsigned long cb_priv)
{
	u64 tid = mlxsw_core_tid_get(mlxsw_core);
	struct mlxsw_reg_trans *trans;
	int err;

	trans = kzalloc(sizeof(*trans), GFP_KERNEL);
	if (!trans)
		return -ENOMEM;

	err = mlxsw_emad_reg_access(mlxsw_core, reg, payload, type, trans,
				    bulk_list, cb, cb_priv, tid);
	if (err) {
		kfree(trans);
		return err;
	}
	return 0;
}

int mlxsw_reg_trans_query(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_reg_info *reg, char *payload,
			  struct list_head *bulk_list,
			  mlxsw_reg_trans_cb_t *cb, unsigned long cb_priv)
{
	return mlxsw_core_reg_access_emad(mlxsw_core, reg, payload,
					  MLXSW_CORE_REG_ACCESS_TYPE_QUERY,
					  bulk_list, cb, cb_priv);
}
EXPORT_SYMBOL(mlxsw_reg_trans_query);

int mlxsw_reg_trans_write(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_reg_info *reg, char *payload,
			  struct list_head *bulk_list,
			  mlxsw_reg_trans_cb_t *cb, unsigned long cb_priv)
{
	return mlxsw_core_reg_access_emad(mlxsw_core, reg, payload,
					  MLXSW_CORE_REG_ACCESS_TYPE_WRITE,
					  bulk_list, cb, cb_priv);
}
EXPORT_SYMBOL(mlxsw_reg_trans_write);

static int mlxsw_reg_trans_wait(struct mlxsw_reg_trans *trans)
{
	struct mlxsw_core *mlxsw_core = trans->core;
	int err;

	wait_for_completion(&trans->completion);
	cancel_delayed_work_sync(&trans->timeout_dw);
	err = trans->err;

	if (trans->retries)
		dev_warn(mlxsw_core->bus_info->dev, "EMAD retries (%d/%d) (tid=%llx)\n",
			 trans->retries, MLXSW_EMAD_MAX_RETRY, trans->tid);
	if (err)
		dev_err(mlxsw_core->bus_info->dev, "EMAD reg access failed (tid=%llx,reg_id=%x(%s),type=%s,status=%x(%s))\n",
			trans->tid, trans->reg->id,
			mlxsw_reg_id_str(trans->reg->id),
			mlxsw_core_reg_access_type_str(trans->type),
			trans->emad_status,
			mlxsw_emad_op_tlv_status_str(trans->emad_status));

	list_del(&trans->bulk_list);
	kfree_rcu(trans, rcu);
	return err;
}

int mlxsw_reg_trans_bulk_wait(struct list_head *bulk_list)
{
	struct mlxsw_reg_trans *trans;
	struct mlxsw_reg_trans *tmp;
	int sum_err = 0;
	int err;

	list_for_each_entry_safe(trans, tmp, bulk_list, bulk_list) {
		err = mlxsw_reg_trans_wait(trans);
		if (err && sum_err == 0)
			sum_err = err; /* first error to be returned */
	}
	return sum_err;
}
EXPORT_SYMBOL(mlxsw_reg_trans_bulk_wait);

static int mlxsw_core_reg_access_cmd(struct mlxsw_core *mlxsw_core,
				     const struct mlxsw_reg_info *reg,
				     char *payload,
				     enum mlxsw_core_reg_access_type type)
{
	enum mlxsw_emad_op_tlv_status status;
	int err, n_retry;
	char *in_mbox, *out_mbox, *tmp;

	dev_dbg(mlxsw_core->bus_info->dev, "Reg cmd access (reg_id=%x(%s),type=%s)\n",
		reg->id, mlxsw_reg_id_str(reg->id),
		mlxsw_core_reg_access_type_str(type));

	in_mbox = mlxsw_cmd_mbox_alloc();
	if (!in_mbox)
		return -ENOMEM;

	out_mbox = mlxsw_cmd_mbox_alloc();
	if (!out_mbox) {
		err = -ENOMEM;
		goto free_in_mbox;
	}

	mlxsw_emad_pack_op_tlv(in_mbox, reg, type,
			       mlxsw_core_tid_get(mlxsw_core));
	tmp = in_mbox + MLXSW_EMAD_OP_TLV_LEN * sizeof(u32);
	mlxsw_emad_pack_reg_tlv(tmp, reg, payload);

	n_retry = 0;
retry:
	err = mlxsw_cmd_access_reg(mlxsw_core, in_mbox, out_mbox);
	if (!err) {
		err = mlxsw_emad_process_status(out_mbox, &status);
		if (err) {
			if (err == -EAGAIN && n_retry++ < MLXSW_EMAD_MAX_RETRY)
				goto retry;
			dev_err(mlxsw_core->bus_info->dev, "Reg cmd access status failed (status=%x(%s))\n",
				status, mlxsw_emad_op_tlv_status_str(status));
		}
	}

	if (!err)
		memcpy(payload, mlxsw_emad_reg_payload(out_mbox),
		       reg->len);

	mlxsw_cmd_mbox_free(out_mbox);
free_in_mbox:
	mlxsw_cmd_mbox_free(in_mbox);
	if (err)
		dev_err(mlxsw_core->bus_info->dev, "Reg cmd access failed (reg_id=%x(%s),type=%s)\n",
			reg->id, mlxsw_reg_id_str(reg->id),
			mlxsw_core_reg_access_type_str(type));
	return err;
}

static void mlxsw_core_reg_access_cb(struct mlxsw_core *mlxsw_core,
				     char *payload, size_t payload_len,
				     unsigned long cb_priv)
{
	char *orig_payload = (char *) cb_priv;

	memcpy(orig_payload, payload, payload_len);
}

static int mlxsw_core_reg_access(struct mlxsw_core *mlxsw_core,
				 const struct mlxsw_reg_info *reg,
				 char *payload,
				 enum mlxsw_core_reg_access_type type)
{
	LIST_HEAD(bulk_list);
	int err;

	/* During initialization EMAD interface is not available to us,
	 * so we default to command interface. We switch to EMAD interface
	 * after setting the appropriate traps.
	 */
	if (!mlxsw_core->emad.use_emad)
		return mlxsw_core_reg_access_cmd(mlxsw_core, reg,
						 payload, type);

	err = mlxsw_core_reg_access_emad(mlxsw_core, reg,
					 payload, type, &bulk_list,
					 mlxsw_core_reg_access_cb,
					 (unsigned long) payload);
	if (err)
		return err;
	return mlxsw_reg_trans_bulk_wait(&bulk_list);
}

int mlxsw_reg_query(struct mlxsw_core *mlxsw_core,
		    const struct mlxsw_reg_info *reg, char *payload)
{
	return mlxsw_core_reg_access(mlxsw_core, reg, payload,
				     MLXSW_CORE_REG_ACCESS_TYPE_QUERY);
}
EXPORT_SYMBOL(mlxsw_reg_query);

int mlxsw_reg_write(struct mlxsw_core *mlxsw_core,
		    const struct mlxsw_reg_info *reg, char *payload)
{
	return mlxsw_core_reg_access(mlxsw_core, reg, payload,
				     MLXSW_CORE_REG_ACCESS_TYPE_WRITE);
}
EXPORT_SYMBOL(mlxsw_reg_write);

void mlxsw_core_skb_receive(struct mlxsw_core *mlxsw_core, struct sk_buff *skb,
			    struct mlxsw_rx_info *rx_info)
{
	struct mlxsw_rx_listener_item *rxl_item;
	const struct mlxsw_rx_listener *rxl;
	u8 local_port;
	bool found = false;

	if (rx_info->is_lag) {
		dev_dbg_ratelimited(mlxsw_core->bus_info->dev, "%s: lag_id = %d, lag_port_index = 0x%x\n",
				    __func__, rx_info->u.lag_id,
				    rx_info->trap_id);
		/* Upper layer does not care if the skb came from LAG or not,
		 * so just get the local_port for the lag port and push it up.
		 */
		local_port = mlxsw_core_lag_mapping_get(mlxsw_core,
							rx_info->u.lag_id,
							rx_info->lag_port_index);
	} else {
		local_port = rx_info->u.sys_port;
	}

	dev_dbg_ratelimited(mlxsw_core->bus_info->dev, "%s: local_port = %d, trap_id = 0x%x\n",
			    __func__, local_port, rx_info->trap_id);

	if ((rx_info->trap_id >= MLXSW_TRAP_ID_MAX) ||
	    (local_port >= mlxsw_core->max_ports))
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

	rxl->func(skb, local_port, rxl_item->priv);
	return;

drop:
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL(mlxsw_core_skb_receive);

static int mlxsw_core_lag_mapping_index(struct mlxsw_core *mlxsw_core,
					u16 lag_id, u8 port_index)
{
	return MLXSW_CORE_RES_GET(mlxsw_core, MAX_LAG_MEMBERS) * lag_id +
	       port_index;
}

void mlxsw_core_lag_mapping_set(struct mlxsw_core *mlxsw_core,
				u16 lag_id, u8 port_index, u8 local_port)
{
	int index = mlxsw_core_lag_mapping_index(mlxsw_core,
						 lag_id, port_index);

	mlxsw_core->lag.mapping[index] = local_port;
}
EXPORT_SYMBOL(mlxsw_core_lag_mapping_set);

u8 mlxsw_core_lag_mapping_get(struct mlxsw_core *mlxsw_core,
			      u16 lag_id, u8 port_index)
{
	int index = mlxsw_core_lag_mapping_index(mlxsw_core,
						 lag_id, port_index);

	return mlxsw_core->lag.mapping[index];
}
EXPORT_SYMBOL(mlxsw_core_lag_mapping_get);

void mlxsw_core_lag_mapping_clear(struct mlxsw_core *mlxsw_core,
				  u16 lag_id, u8 local_port)
{
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_core, MAX_LAG_MEMBERS); i++) {
		int index = mlxsw_core_lag_mapping_index(mlxsw_core,
							 lag_id, i);

		if (mlxsw_core->lag.mapping[index] == local_port)
			mlxsw_core->lag.mapping[index] = 0;
	}
}
EXPORT_SYMBOL(mlxsw_core_lag_mapping_clear);

bool mlxsw_core_res_valid(struct mlxsw_core *mlxsw_core,
			  enum mlxsw_res_id res_id)
{
	return mlxsw_res_valid(&mlxsw_core->res, res_id);
}
EXPORT_SYMBOL(mlxsw_core_res_valid);

u64 mlxsw_core_res_get(struct mlxsw_core *mlxsw_core,
		       enum mlxsw_res_id res_id)
{
	return mlxsw_res_get(&mlxsw_core->res, res_id);
}
EXPORT_SYMBOL(mlxsw_core_res_get);

int mlxsw_core_port_init(struct mlxsw_core *mlxsw_core, u8 local_port)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;
	int err;

	mlxsw_core_port->local_port = local_port;
	err = devlink_port_register(devlink, devlink_port, local_port);
	if (err)
		memset(mlxsw_core_port, 0, sizeof(*mlxsw_core_port));
	return err;
}
EXPORT_SYMBOL(mlxsw_core_port_init);

void mlxsw_core_port_fini(struct mlxsw_core *mlxsw_core, u8 local_port)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	devlink_port_unregister(devlink_port);
	memset(mlxsw_core_port, 0, sizeof(*mlxsw_core_port));
}
EXPORT_SYMBOL(mlxsw_core_port_fini);

void mlxsw_core_port_eth_set(struct mlxsw_core *mlxsw_core, u8 local_port,
			     void *port_driver_priv, struct net_device *dev,
			     bool split, u32 split_group)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	mlxsw_core_port->port_driver_priv = port_driver_priv;
	if (split)
		devlink_port_split_set(devlink_port, split_group);
	devlink_port_type_eth_set(devlink_port, dev);
}
EXPORT_SYMBOL(mlxsw_core_port_eth_set);

void mlxsw_core_port_ib_set(struct mlxsw_core *mlxsw_core, u8 local_port,
			    void *port_driver_priv)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	mlxsw_core_port->port_driver_priv = port_driver_priv;
	devlink_port_type_ib_set(devlink_port, NULL);
}
EXPORT_SYMBOL(mlxsw_core_port_ib_set);

void mlxsw_core_port_clear(struct mlxsw_core *mlxsw_core, u8 local_port,
			   void *port_driver_priv)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	mlxsw_core_port->port_driver_priv = port_driver_priv;
	devlink_port_type_clear(devlink_port);
}
EXPORT_SYMBOL(mlxsw_core_port_clear);

enum devlink_port_type mlxsw_core_port_type_get(struct mlxsw_core *mlxsw_core,
						u8 local_port)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	return devlink_port->type;
}
EXPORT_SYMBOL(mlxsw_core_port_type_get);

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

int mlxsw_core_schedule_dw(struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work(mlxsw_wq, dwork, delay);
}
EXPORT_SYMBOL(mlxsw_core_schedule_dw);

bool mlxsw_core_schedule_work(struct work_struct *work)
{
	return queue_work(mlxsw_owq, work);
}
EXPORT_SYMBOL(mlxsw_core_schedule_work);

void mlxsw_core_flush_owq(void)
{
	flush_workqueue(mlxsw_owq);
}
EXPORT_SYMBOL(mlxsw_core_flush_owq);

static int __init mlxsw_core_module_init(void)
{
	int err;

	mlxsw_wq = alloc_workqueue(mlxsw_core_driver_name, WQ_MEM_RECLAIM, 0);
	if (!mlxsw_wq)
		return -ENOMEM;
	mlxsw_owq = alloc_ordered_workqueue("%s_ordered", WQ_MEM_RECLAIM,
					    mlxsw_core_driver_name);
	if (!mlxsw_owq) {
		err = -ENOMEM;
		goto err_alloc_ordered_workqueue;
	}
	return 0;

err_alloc_ordered_workqueue:
	destroy_workqueue(mlxsw_wq);
	return err;
}

static void __exit mlxsw_core_module_exit(void)
{
	destroy_workqueue(mlxsw_owq);
	destroy_workqueue(mlxsw_wq);
}

module_init(mlxsw_core_module_init);
module_exit(mlxsw_core_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox switch device core driver");
