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
#include <linux/wait.h>
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
#include <asm/byteorder.h>
#include <net/devlink.h>

#include "core.h"
#include "item.h"
#include "cmd.h"
#include "port.h"
#include "trap.h"
#include "emad.h"
#include "reg.h"

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
	struct list_head event_listener_list;
	struct {
		struct sk_buff *resp_skb;
		u64 tid;
		wait_queue_head_t wait;
		bool trans_active;
		struct mutex lock; /* One EMAD transaction at a time. */
		bool use_emad;
	} emad;
	struct mlxsw_core_pcpu_stats __percpu *pcpu_stats;
	struct dentry *dbg_dir;
	struct {
		struct debugfs_blob_wrapper vsd_blob;
		struct debugfs_blob_wrapper psid_blob;
	} dbg;
	struct {
		u8 *mapping; /* lag_id+port_index to local_port mapping */
	} lag;
	struct mlxsw_hwmon *hwmon;
	unsigned long driver_priv[0];
	/* driver_priv has to be always the last item */
};

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
				   struct mlxsw_core *mlxsw_core)
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
	mlxsw_emad_op_tlv_tid_set(op_tlv, mlxsw_core->emad.tid);
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
				 struct mlxsw_core *mlxsw_core)
{
	char *buf;

	buf = skb_push(skb, MLXSW_EMAD_END_TLV_LEN * sizeof(u32));
	mlxsw_emad_pack_end_tlv(buf);

	buf = skb_push(skb, reg->len + sizeof(u32));
	mlxsw_emad_pack_reg_tlv(buf, reg, payload);

	buf = skb_push(skb, MLXSW_EMAD_OP_TLV_LEN * sizeof(u32));
	mlxsw_emad_pack_op_tlv(buf, reg, type, mlxsw_core);

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

#define MLXSW_EMAD_TIMEOUT_MS 200

static int __mlxsw_emad_transmit(struct mlxsw_core *mlxsw_core,
				 struct sk_buff *skb,
				 const struct mlxsw_tx_info *tx_info)
{
	int err;
	int ret;

	mlxsw_core->emad.trans_active = true;

	err = mlxsw_core_skb_transmit(mlxsw_core->driver_priv, skb, tx_info);
	if (err) {
		dev_err(mlxsw_core->bus_info->dev, "Failed to transmit EMAD (tid=%llx)\n",
			mlxsw_core->emad.tid);
		dev_kfree_skb(skb);
		goto trans_inactive_out;
	}

	ret = wait_event_timeout(mlxsw_core->emad.wait,
				 !(mlxsw_core->emad.trans_active),
				 msecs_to_jiffies(MLXSW_EMAD_TIMEOUT_MS));
	if (!ret) {
		dev_warn(mlxsw_core->bus_info->dev, "EMAD timed-out (tid=%llx)\n",
			 mlxsw_core->emad.tid);
		err = -EIO;
		goto trans_inactive_out;
	}

	return 0;

trans_inactive_out:
	mlxsw_core->emad.trans_active = false;
	return err;
}

static int mlxsw_emad_process_status(struct mlxsw_core *mlxsw_core,
				     char *op_tlv)
{
	enum mlxsw_emad_op_tlv_status status;
	u64 tid;

	status = mlxsw_emad_op_tlv_status_get(op_tlv);
	tid = mlxsw_emad_op_tlv_tid_get(op_tlv);

	switch (status) {
	case MLXSW_EMAD_OP_TLV_STATUS_SUCCESS:
		return 0;
	case MLXSW_EMAD_OP_TLV_STATUS_BUSY:
	case MLXSW_EMAD_OP_TLV_STATUS_MESSAGE_RECEIPT_ACK:
		dev_warn(mlxsw_core->bus_info->dev, "Reg access status again (tid=%llx,status=%x(%s))\n",
			 tid, status, mlxsw_emad_op_tlv_status_str(status));
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
		dev_err(mlxsw_core->bus_info->dev, "Reg access status failed (tid=%llx,status=%x(%s))\n",
			tid, status, mlxsw_emad_op_tlv_status_str(status));
		return -EIO;
	}
}

static int mlxsw_emad_process_status_skb(struct mlxsw_core *mlxsw_core,
					 struct sk_buff *skb)
{
	return mlxsw_emad_process_status(mlxsw_core, mlxsw_emad_op_tlv(skb));
}

static int mlxsw_emad_transmit(struct mlxsw_core *mlxsw_core,
			       struct sk_buff *skb,
			       const struct mlxsw_tx_info *tx_info)
{
	struct sk_buff *trans_skb;
	int n_retry;
	int err;

	n_retry = 0;
retry:
	/* We copy the EMAD to a new skb, since we might need
	 * to retransmit it in case of failure.
	 */
	trans_skb = skb_copy(skb, GFP_KERNEL);
	if (!trans_skb) {
		err = -ENOMEM;
		goto out;
	}

	err = __mlxsw_emad_transmit(mlxsw_core, trans_skb, tx_info);
	if (!err) {
		struct sk_buff *resp_skb = mlxsw_core->emad.resp_skb;

		err = mlxsw_emad_process_status_skb(mlxsw_core, resp_skb);
		if (err)
			dev_kfree_skb(resp_skb);
		if (!err || err != -EAGAIN)
			goto out;
	}
	if (n_retry++ < MLXSW_EMAD_MAX_RETRY)
		goto retry;

out:
	dev_kfree_skb(skb);
	mlxsw_core->emad.tid++;
	return err;
}

static void mlxsw_emad_rx_listener_func(struct sk_buff *skb, u8 local_port,
					void *priv)
{
	struct mlxsw_core *mlxsw_core = priv;

	if (mlxsw_emad_is_resp(skb) &&
	    mlxsw_core->emad.trans_active &&
	    mlxsw_emad_get_tid(skb) == mlxsw_core->emad.tid) {
		mlxsw_core->emad.resp_skb = skb;
		mlxsw_core->emad.trans_active = false;
		wake_up(&mlxsw_core->emad.wait);
	} else {
		dev_kfree_skb(skb);
	}
}

static const struct mlxsw_rx_listener mlxsw_emad_rx_listener = {
	.func = mlxsw_emad_rx_listener_func,
	.local_port = MLXSW_PORT_DONT_CARE,
	.trap_id = MLXSW_TRAP_ID_ETHEMAD,
};

static int mlxsw_emad_traps_set(struct mlxsw_core *mlxsw_core)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int err;

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_EMAD);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
	if (err)
		return err;

	mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_TRAP_TO_CPU,
			    MLXSW_TRAP_ID_ETHEMAD);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(hpkt), hpkt_pl);
}

static int mlxsw_emad_init(struct mlxsw_core *mlxsw_core)
{
	int err;

	/* Set the upper 32 bits of the transaction ID field to a random
	 * number. This allows us to discard EMADs addressed to other
	 * devices.
	 */
	get_random_bytes(&mlxsw_core->emad.tid, 4);
	mlxsw_core->emad.tid = mlxsw_core->emad.tid << 32;

	init_waitqueue_head(&mlxsw_core->emad.wait);
	mlxsw_core->emad.trans_active = false;
	mutex_init(&mlxsw_core->emad.lock);

	err = mlxsw_core_rx_listener_register(mlxsw_core,
					      &mlxsw_emad_rx_listener,
					      mlxsw_core);
	if (err)
		return err;

	err = mlxsw_emad_traps_set(mlxsw_core);
	if (err)
		goto err_emad_trap_set;

	mlxsw_core->emad.use_emad = true;

	return 0;

err_emad_trap_set:
	mlxsw_core_rx_listener_unregister(mlxsw_core,
					  &mlxsw_emad_rx_listener,
					  mlxsw_core);
	return err;
}

static void mlxsw_emad_fini(struct mlxsw_core *mlxsw_core)
{
	char hpkt_pl[MLXSW_REG_HPKT_LEN];

	mlxsw_core->emad.use_emad = false;
	mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_DISCARD,
			    MLXSW_TRAP_ID_ETHEMAD);
	mlxsw_reg_write(mlxsw_core, MLXSW_REG(hpkt), hpkt_pl);

	mlxsw_core_rx_listener_unregister(mlxsw_core,
					  &mlxsw_emad_rx_listener,
					  mlxsw_core);
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

static int mlxsw_devlink_port_split(struct devlink *devlink,
				    unsigned int port_index,
				    unsigned int count)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	if (port_index >= MLXSW_PORT_MAX_PORTS)
		return -EINVAL;
	if (!mlxsw_core->driver->port_split)
		return -EOPNOTSUPP;
	return mlxsw_core->driver->port_split(mlxsw_core->driver_priv,
					      port_index, count);
}

static int mlxsw_devlink_port_unsplit(struct devlink *devlink,
				      unsigned int port_index)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	if (port_index >= MLXSW_PORT_MAX_PORTS)
		return -EINVAL;
	if (!mlxsw_core->driver->port_unsplit)
		return -EOPNOTSUPP;
	return mlxsw_core->driver->port_unsplit(mlxsw_core->driver_priv,
						port_index);
}

static const struct devlink_ops mlxsw_devlink_ops = {
	.port_split	= mlxsw_devlink_port_split,
	.port_unsplit	= mlxsw_devlink_port_unsplit,
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

	mlxsw_core->pcpu_stats =
		netdev_alloc_pcpu_stats(struct mlxsw_core_pcpu_stats);
	if (!mlxsw_core->pcpu_stats) {
		err = -ENOMEM;
		goto err_alloc_stats;
	}

	if (mlxsw_driver->profile->used_max_lag &&
	    mlxsw_driver->profile->used_max_port_per_lag) {
		alloc_size = sizeof(u8) * mlxsw_driver->profile->max_lag *
			     mlxsw_driver->profile->max_port_per_lag;
		mlxsw_core->lag.mapping = kzalloc(alloc_size, GFP_KERNEL);
		if (!mlxsw_core->lag.mapping) {
			err = -ENOMEM;
			goto err_alloc_lag_mapping;
		}
	}

	err = mlxsw_bus->init(bus_priv, mlxsw_core, mlxsw_driver->profile);
	if (err)
		goto err_bus_init;

	err = mlxsw_emad_init(mlxsw_core);
	if (err)
		goto err_emad_init;

	err = mlxsw_hwmon_init(mlxsw_core, mlxsw_bus_info, &mlxsw_core->hwmon);
	if (err)
		goto err_hwmon_init;

	err = devlink_register(devlink, mlxsw_bus_info->dev);
	if (err)
		goto err_devlink_register;

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
	devlink_unregister(devlink);
err_devlink_register:
err_hwmon_init:
	mlxsw_emad_fini(mlxsw_core);
err_emad_init:
	mlxsw_bus->fini(bus_priv);
err_bus_init:
	kfree(mlxsw_core->lag.mapping);
err_alloc_lag_mapping:
	free_percpu(mlxsw_core->pcpu_stats);
err_alloc_stats:
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

	mlxsw_core_debugfs_fini(mlxsw_core);
	mlxsw_core->driver->fini(mlxsw_core->driver_priv);
	devlink_unregister(devlink);
	mlxsw_emad_fini(mlxsw_core);
	mlxsw_core->bus->fini(mlxsw_core->bus_priv);
	kfree(mlxsw_core->lag.mapping);
	free_percpu(mlxsw_core->pcpu_stats);
	devlink_free(devlink);
	mlxsw_core_driver_put(device_kind);
}
EXPORT_SYMBOL(mlxsw_core_bus_device_unregister);

static struct mlxsw_core *__mlxsw_core_get(void *driver_priv)
{
	return container_of(driver_priv, struct mlxsw_core, driver_priv);
}

bool mlxsw_core_skb_transmit_busy(void *driver_priv,
				  const struct mlxsw_tx_info *tx_info)
{
	struct mlxsw_core *mlxsw_core = __mlxsw_core_get(driver_priv);

	return mlxsw_core->bus->skb_transmit_busy(mlxsw_core->bus_priv,
						  tx_info);
}
EXPORT_SYMBOL(mlxsw_core_skb_transmit_busy);

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

static int mlxsw_core_reg_access_emad(struct mlxsw_core *mlxsw_core,
				      const struct mlxsw_reg_info *reg,
				      char *payload,
				      enum mlxsw_core_reg_access_type type)
{
	int err;
	char *op_tlv;
	struct sk_buff *skb;
	struct mlxsw_tx_info tx_info = {
		.local_port = MLXSW_PORT_CPU_PORT,
		.is_emad = true,
	};

	skb = mlxsw_emad_alloc(mlxsw_core, reg->len);
	if (!skb)
		return -ENOMEM;

	mlxsw_emad_construct(skb, reg, payload, type, mlxsw_core);
	mlxsw_core->driver->txhdr_construct(skb, &tx_info);

	dev_dbg(mlxsw_core->bus_info->dev, "EMAD send (tid=%llx)\n",
		mlxsw_core->emad.tid);
	mlxsw_core_buf_dump_dbg(mlxsw_core, skb->data, skb->len);

	err = mlxsw_emad_transmit(mlxsw_core, skb, &tx_info);
	if (!err) {
		op_tlv = mlxsw_emad_op_tlv(mlxsw_core->emad.resp_skb);
		memcpy(payload, mlxsw_emad_reg_payload(op_tlv),
		       reg->len);

		dev_dbg(mlxsw_core->bus_info->dev, "EMAD recv (tid=%llx)\n",
			mlxsw_core->emad.tid - 1);
		mlxsw_core_buf_dump_dbg(mlxsw_core,
					mlxsw_core->emad.resp_skb->data,
					mlxsw_core->emad.resp_skb->len);

		dev_kfree_skb(mlxsw_core->emad.resp_skb);
	}

	return err;
}

static int mlxsw_core_reg_access_cmd(struct mlxsw_core *mlxsw_core,
				     const struct mlxsw_reg_info *reg,
				     char *payload,
				     enum mlxsw_core_reg_access_type type)
{
	int err, n_retry;
	char *in_mbox, *out_mbox, *tmp;

	in_mbox = mlxsw_cmd_mbox_alloc();
	if (!in_mbox)
		return -ENOMEM;

	out_mbox = mlxsw_cmd_mbox_alloc();
	if (!out_mbox) {
		err = -ENOMEM;
		goto free_in_mbox;
	}

	mlxsw_emad_pack_op_tlv(in_mbox, reg, type, mlxsw_core);
	tmp = in_mbox + MLXSW_EMAD_OP_TLV_LEN * sizeof(u32);
	mlxsw_emad_pack_reg_tlv(tmp, reg, payload);

	n_retry = 0;
retry:
	err = mlxsw_cmd_access_reg(mlxsw_core, in_mbox, out_mbox);
	if (!err) {
		err = mlxsw_emad_process_status(mlxsw_core, out_mbox);
		if (err == -EAGAIN && n_retry++ < MLXSW_EMAD_MAX_RETRY)
			goto retry;
	}

	if (!err)
		memcpy(payload, mlxsw_emad_reg_payload(out_mbox),
		       reg->len);

	mlxsw_core->emad.tid++;
	mlxsw_cmd_mbox_free(out_mbox);
free_in_mbox:
	mlxsw_cmd_mbox_free(in_mbox);
	return err;
}

static int mlxsw_core_reg_access(struct mlxsw_core *mlxsw_core,
				 const struct mlxsw_reg_info *reg,
				 char *payload,
				 enum mlxsw_core_reg_access_type type)
{
	u64 cur_tid;
	int err;

	if (mutex_lock_interruptible(&mlxsw_core->emad.lock)) {
		dev_err(mlxsw_core->bus_info->dev, "Reg access interrupted (reg_id=%x(%s),type=%s)\n",
			reg->id, mlxsw_reg_id_str(reg->id),
			mlxsw_core_reg_access_type_str(type));
		return -EINTR;
	}

	cur_tid = mlxsw_core->emad.tid;
	dev_dbg(mlxsw_core->bus_info->dev, "Reg access (tid=%llx,reg_id=%x(%s),type=%s)\n",
		cur_tid, reg->id, mlxsw_reg_id_str(reg->id),
		mlxsw_core_reg_access_type_str(type));

	/* During initialization EMAD interface is not available to us,
	 * so we default to command interface. We switch to EMAD interface
	 * after setting the appropriate traps.
	 */
	if (!mlxsw_core->emad.use_emad)
		err = mlxsw_core_reg_access_cmd(mlxsw_core, reg,
						payload, type);
	else
		err = mlxsw_core_reg_access_emad(mlxsw_core, reg,
						 payload, type);

	if (err)
		dev_err(mlxsw_core->bus_info->dev, "Reg access failed (tid=%llx,reg_id=%x(%s),type=%s)\n",
			cur_tid, reg->id, mlxsw_reg_id_str(reg->id),
			mlxsw_core_reg_access_type_str(type));

	mutex_unlock(&mlxsw_core->emad.lock);
	return err;
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
	struct mlxsw_core_pcpu_stats *pcpu_stats;
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

static int mlxsw_core_lag_mapping_index(struct mlxsw_core *mlxsw_core,
					u16 lag_id, u8 port_index)
{
	return mlxsw_core->driver->profile->max_port_per_lag * lag_id +
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

	for (i = 0; i < mlxsw_core->driver->profile->max_port_per_lag; i++) {
		int index = mlxsw_core_lag_mapping_index(mlxsw_core,
							 lag_id, i);

		if (mlxsw_core->lag.mapping[index] == local_port)
			mlxsw_core->lag.mapping[index] = 0;
	}
}
EXPORT_SYMBOL(mlxsw_core_lag_mapping_clear);

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
