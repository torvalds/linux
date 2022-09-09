// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

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
#include <linux/firmware.h>
#include <asm/byteorder.h>
#include <net/devlink.h>
#include <trace/events/devlink.h>

#include "core.h"
#include "core_env.h"
#include "item.h"
#include "cmd.h"
#include "port.h"
#include "trap.h"
#include "emad.h"
#include "reg.h"
#include "resources.h"
#include "../mlxfw/mlxfw.h"

static LIST_HEAD(mlxsw_core_driver_list);
static DEFINE_SPINLOCK(mlxsw_core_driver_list_lock);

static const char mlxsw_core_driver_name[] = "mlxsw_core";

static struct workqueue_struct *mlxsw_wq;
static struct workqueue_struct *mlxsw_owq;

struct mlxsw_core_port {
	struct devlink_port devlink_port;
	void *port_driver_priv;
	u16 local_port;
	struct mlxsw_linecard *linecard;
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
		bool enable_string_tlv;
	} emad;
	struct {
		u16 *mapping; /* lag_id+port_index to local_port mapping */
	} lag;
	struct mlxsw_res res;
	struct mlxsw_hwmon *hwmon;
	struct mlxsw_thermal *thermal;
	struct mlxsw_linecards *linecards;
	struct mlxsw_core_port *ports;
	unsigned int max_ports;
	atomic_t active_ports_count;
	bool fw_flash_in_progress;
	struct {
		struct devlink_health_reporter *fw_fatal;
	} health;
	struct mlxsw_env *env;
	unsigned long driver_priv[];
	/* driver_priv has to be always the last item */
};

struct mlxsw_linecards *mlxsw_core_linecards(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->linecards;
}

void mlxsw_core_linecards_set(struct mlxsw_core *mlxsw_core,
			      struct mlxsw_linecards *linecards)
{
	mlxsw_core->linecards = linecards;
}

#define MLXSW_PORT_MAX_PORTS_DEFAULT	0x40

static u64 mlxsw_ports_occ_get(void *priv)
{
	struct mlxsw_core *mlxsw_core = priv;

	return atomic_read(&mlxsw_core->active_ports_count);
}

static int mlxsw_core_resources_ports_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct devlink_resource_size_params ports_num_params;
	u32 max_ports;

	max_ports = mlxsw_core->max_ports - 1;
	devlink_resource_size_params_init(&ports_num_params, max_ports,
					  max_ports, 1,
					  DEVLINK_RESOURCE_UNIT_ENTRY);

	return devl_resource_register(devlink,
				      DEVLINK_RESOURCE_GENERIC_NAME_PORTS,
				      max_ports, MLXSW_CORE_RESOURCE_PORTS,
				      DEVLINK_RESOURCE_ID_PARENT_TOP,
				      &ports_num_params);
}

static int mlxsw_ports_init(struct mlxsw_core *mlxsw_core, bool reload)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	int err;

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

	if (!reload) {
		err = mlxsw_core_resources_ports_register(mlxsw_core);
		if (err)
			goto err_resources_ports_register;
	}
	atomic_set(&mlxsw_core->active_ports_count, 0);
	devl_resource_occ_get_register(devlink, MLXSW_CORE_RESOURCE_PORTS,
				       mlxsw_ports_occ_get, mlxsw_core);

	return 0;

err_resources_ports_register:
	kfree(mlxsw_core->ports);
	return err;
}

static void mlxsw_ports_fini(struct mlxsw_core *mlxsw_core, bool reload)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);

	devl_resource_occ_get_unregister(devlink, MLXSW_CORE_RESOURCE_PORTS);
	if (!reload)
		devl_resources_unregister(priv_to_devlink(mlxsw_core));

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

bool
mlxsw_core_fw_rev_minor_subminor_validate(const struct mlxsw_fw_rev *rev,
					  const struct mlxsw_fw_rev *req_rev)
{
	return rev->minor > req_rev->minor ||
	       (rev->minor == req_rev->minor &&
		rev->subminor >= req_rev->subminor);
}
EXPORT_SYMBOL(mlxsw_core_fw_rev_minor_subminor_validate);

struct mlxsw_rx_listener_item {
	struct list_head list;
	struct mlxsw_rx_listener rxl;
	void *priv;
	bool enabled;
};

struct mlxsw_event_listener_item {
	struct list_head list;
	struct mlxsw_core *mlxsw_core;
	struct mlxsw_event_listener el;
	void *priv;
};

static const u8 mlxsw_core_trap_groups[] = {
	MLXSW_REG_HTGT_TRAP_GROUP_EMAD,
	MLXSW_REG_HTGT_TRAP_GROUP_CORE_EVENT,
};

static int mlxsw_core_trap_groups_set(struct mlxsw_core *mlxsw_core)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];
	int err;
	int i;

	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_TXRX))
		return 0;

	for (i = 0; i < ARRAY_SIZE(mlxsw_core_trap_groups); i++) {
		mlxsw_reg_htgt_pack(htgt_pl, mlxsw_core_trap_groups[i],
				    MLXSW_REG_HTGT_INVALID_POLICER,
				    MLXSW_REG_HTGT_DEFAULT_PRIORITY,
				    MLXSW_REG_HTGT_DEFAULT_TC);
		err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
		if (err)
			return err;
	}
	return 0;
}

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

/* emad_string_tlv_type
 * Type of the TLV.
 * Must be set to 0x2 (string TLV).
 */
MLXSW_ITEM32(emad, string_tlv, type, 0x00, 27, 5);

/* emad_string_tlv_len
 * Length of the string TLV in u32.
 */
MLXSW_ITEM32(emad, string_tlv, len, 0x00, 16, 11);

#define MLXSW_EMAD_STRING_TLV_STRING_LEN 128

/* emad_string_tlv_string
 * String provided by the device's firmware in case of erroneous register access
 */
MLXSW_ITEM_BUF(emad, string_tlv, string, 0x04,
	       MLXSW_EMAD_STRING_TLV_STRING_LEN);

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

static void mlxsw_emad_pack_string_tlv(char *string_tlv)
{
	mlxsw_emad_string_tlv_type_set(string_tlv, MLXSW_EMAD_TLV_TYPE_STRING);
	mlxsw_emad_string_tlv_len_set(string_tlv, MLXSW_EMAD_STRING_TLV_LEN);
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
				 u64 tid, bool enable_string_tlv)
{
	char *buf;

	buf = skb_push(skb, MLXSW_EMAD_END_TLV_LEN * sizeof(u32));
	mlxsw_emad_pack_end_tlv(buf);

	buf = skb_push(skb, reg->len + sizeof(u32));
	mlxsw_emad_pack_reg_tlv(buf, reg, payload);

	if (enable_string_tlv) {
		buf = skb_push(skb, MLXSW_EMAD_STRING_TLV_LEN * sizeof(u32));
		mlxsw_emad_pack_string_tlv(buf);
	}

	buf = skb_push(skb, MLXSW_EMAD_OP_TLV_LEN * sizeof(u32));
	mlxsw_emad_pack_op_tlv(buf, reg, type, tid);

	mlxsw_emad_construct_eth_hdr(skb);
}

struct mlxsw_emad_tlv_offsets {
	u16 op_tlv;
	u16 string_tlv;
	u16 reg_tlv;
};

static bool mlxsw_emad_tlv_is_string_tlv(const char *tlv)
{
	u8 tlv_type = mlxsw_emad_string_tlv_type_get(tlv);

	return tlv_type == MLXSW_EMAD_TLV_TYPE_STRING;
}

static void mlxsw_emad_tlv_parse(struct sk_buff *skb)
{
	struct mlxsw_emad_tlv_offsets *offsets =
		(struct mlxsw_emad_tlv_offsets *) skb->cb;

	offsets->op_tlv = MLXSW_EMAD_ETH_HDR_LEN;
	offsets->string_tlv = 0;
	offsets->reg_tlv = MLXSW_EMAD_ETH_HDR_LEN +
			   MLXSW_EMAD_OP_TLV_LEN * sizeof(u32);

	/* If string TLV is present, it must come after the operation TLV. */
	if (mlxsw_emad_tlv_is_string_tlv(skb->data + offsets->reg_tlv)) {
		offsets->string_tlv = offsets->reg_tlv;
		offsets->reg_tlv += MLXSW_EMAD_STRING_TLV_LEN * sizeof(u32);
	}
}

static char *mlxsw_emad_op_tlv(const struct sk_buff *skb)
{
	struct mlxsw_emad_tlv_offsets *offsets =
		(struct mlxsw_emad_tlv_offsets *) skb->cb;

	return ((char *) (skb->data + offsets->op_tlv));
}

static char *mlxsw_emad_string_tlv(const struct sk_buff *skb)
{
	struct mlxsw_emad_tlv_offsets *offsets =
		(struct mlxsw_emad_tlv_offsets *) skb->cb;

	if (!offsets->string_tlv)
		return NULL;

	return ((char *) (skb->data + offsets->string_tlv));
}

static char *mlxsw_emad_reg_tlv(const struct sk_buff *skb)
{
	struct mlxsw_emad_tlv_offsets *offsets =
		(struct mlxsw_emad_tlv_offsets *) skb->cb;

	return ((char *) (skb->data + offsets->reg_tlv));
}

static char *mlxsw_emad_reg_payload(const char *reg_tlv)
{
	return ((char *) (reg_tlv + sizeof(u32)));
}

static char *mlxsw_emad_reg_payload_cmd(const char *mbox)
{
	return ((char *) (mbox + (MLXSW_EMAD_OP_TLV_LEN + 1) * sizeof(u32)));
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
	char *emad_err_string;
	enum mlxsw_emad_op_tlv_status emad_status;
	struct rcu_head rcu;
};

static void mlxsw_emad_process_string_tlv(const struct sk_buff *skb,
					  struct mlxsw_reg_trans *trans)
{
	char *string_tlv;
	char *string;

	string_tlv = mlxsw_emad_string_tlv(skb);
	if (!string_tlv)
		return;

	trans->emad_err_string = kzalloc(MLXSW_EMAD_STRING_TLV_STRING_LEN,
					 GFP_ATOMIC);
	if (!trans->emad_err_string)
		return;

	string = mlxsw_emad_string_tlv_string_data(string_tlv);
	strlcpy(trans->emad_err_string, string,
		MLXSW_EMAD_STRING_TLV_STRING_LEN);
}

#define MLXSW_EMAD_TIMEOUT_DURING_FW_FLASH_MS	3000
#define MLXSW_EMAD_TIMEOUT_MS			200

static void mlxsw_emad_trans_timeout_schedule(struct mlxsw_reg_trans *trans)
{
	unsigned long timeout = msecs_to_jiffies(MLXSW_EMAD_TIMEOUT_MS);

	if (trans->core->fw_flash_in_progress)
		timeout = msecs_to_jiffies(MLXSW_EMAD_TIMEOUT_DURING_FW_FLASH_MS);

	queue_delayed_work(trans->core->emad_wq, &trans->timeout_dw,
			   timeout << trans->retries);
}

static int mlxsw_emad_transmit(struct mlxsw_core *mlxsw_core,
			       struct mlxsw_reg_trans *trans)
{
	struct sk_buff *skb;
	int err;

	skb = skb_clone(trans->tx_skb, GFP_KERNEL);
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

		if (!atomic_dec_and_test(&trans->active))
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
			char *reg_tlv = mlxsw_emad_reg_tlv(skb);

			if (trans->cb)
				trans->cb(mlxsw_core,
					  mlxsw_emad_reg_payload(reg_tlv),
					  trans->reg->len, trans->cb_priv);
		} else {
			mlxsw_emad_process_string_tlv(skb, trans);
		}
		mlxsw_emad_trans_finish(trans, err);
	}
}

/* called with rcu read lock held */
static void mlxsw_emad_rx_listener_func(struct sk_buff *skb, u16 local_port,
					void *priv)
{
	struct mlxsw_core *mlxsw_core = priv;
	struct mlxsw_reg_trans *trans;

	trace_devlink_hwmsg(priv_to_devlink(mlxsw_core), true, 0,
			    skb->data, skb->len);

	mlxsw_emad_tlv_parse(skb);

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

	emad_wq = alloc_workqueue("mlxsw_core_emad", 0, 0);
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
		goto err_trap_register;

	mlxsw_core->emad.use_emad = true;

	return 0;

err_trap_register:
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
					u16 reg_len, bool enable_string_tlv)
{
	struct sk_buff *skb;
	u16 emad_len;

	emad_len = (reg_len + sizeof(u32) + MLXSW_EMAD_ETH_HDR_LEN +
		    (MLXSW_EMAD_OP_TLV_LEN + MLXSW_EMAD_END_TLV_LEN) *
		    sizeof(u32) + mlxsw_core->driver->txhdr_len);
	if (enable_string_tlv)
		emad_len += MLXSW_EMAD_STRING_TLV_LEN * sizeof(u32);
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
	bool enable_string_tlv;
	struct sk_buff *skb;
	int err;

	dev_dbg(mlxsw_core->bus_info->dev, "EMAD reg access (tid=%llx,reg_id=%x(%s),type=%s)\n",
		tid, reg->id, mlxsw_reg_id_str(reg->id),
		mlxsw_core_reg_access_type_str(type));

	/* Since this can be changed during emad_reg_access, read it once and
	 * use the value all the way.
	 */
	enable_string_tlv = mlxsw_core->emad.enable_string_tlv;

	skb = mlxsw_emad_alloc(mlxsw_core, reg->len, enable_string_tlv);
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

	mlxsw_emad_construct(skb, reg, payload, type, trans->tid,
			     enable_string_tlv);
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

int mlxsw_core_fw_flash(struct mlxsw_core *mlxsw_core,
			struct mlxfw_dev *mlxfw_dev,
			const struct firmware *firmware,
			struct netlink_ext_ack *extack)
{
	int err;

	mlxsw_core->fw_flash_in_progress = true;
	err = mlxfw_firmware_flash(mlxfw_dev, firmware, extack);
	mlxsw_core->fw_flash_in_progress = false;

	return err;
}

struct mlxsw_core_fw_info {
	struct mlxfw_dev mlxfw_dev;
	struct mlxsw_core *mlxsw_core;
};

static int mlxsw_core_fw_component_query(struct mlxfw_dev *mlxfw_dev,
					 u16 component_index, u32 *p_max_size,
					 u8 *p_align_bits, u16 *p_max_write_size)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcqi_pl[MLXSW_REG_MCQI_LEN];
	int err;

	mlxsw_reg_mcqi_pack(mcqi_pl, component_index);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mcqi), mcqi_pl);
	if (err)
		return err;
	mlxsw_reg_mcqi_unpack(mcqi_pl, p_max_size, p_align_bits, p_max_write_size);

	*p_align_bits = max_t(u8, *p_align_bits, 2);
	*p_max_write_size = min_t(u16, *p_max_write_size, MLXSW_REG_MCDA_MAX_DATA_LEN);
	return 0;
}

static int mlxsw_core_fw_fsm_lock(struct mlxfw_dev *mlxfw_dev, u32 *fwhandle)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcc_pl[MLXSW_REG_MCC_LEN];
	u8 control_state;
	int err;

	mlxsw_reg_mcc_pack(mcc_pl, 0, 0, 0, 0);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
	if (err)
		return err;

	mlxsw_reg_mcc_unpack(mcc_pl, fwhandle, NULL, &control_state);
	if (control_state != MLXFW_FSM_STATE_IDLE)
		return -EBUSY;

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_LOCK_UPDATE_HANDLE, 0, *fwhandle, 0);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_core_fw_fsm_component_update(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
					      u16 component_index, u32 component_size)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_UPDATE_COMPONENT,
			   component_index, fwhandle, component_size);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_core_fw_fsm_block_download(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
					    u8 *data, u16 size, u32 offset)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcda_pl[MLXSW_REG_MCDA_LEN];

	mlxsw_reg_mcda_pack(mcda_pl, fwhandle, offset, size, data);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mcda), mcda_pl);
}

static int mlxsw_core_fw_fsm_component_verify(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
					      u16 component_index)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_VERIFY_COMPONENT,
			   component_index, fwhandle, 0);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_core_fw_fsm_activate(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_ACTIVATE, 0, fwhandle, 0);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_core_fw_fsm_query_state(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
					 enum mlxfw_fsm_state *fsm_state,
					 enum mlxfw_fsm_state_err *fsm_state_err)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcc_pl[MLXSW_REG_MCC_LEN];
	u8 control_state;
	u8 error_code;
	int err;

	mlxsw_reg_mcc_pack(mcc_pl, 0, 0, fwhandle, 0);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
	if (err)
		return err;

	mlxsw_reg_mcc_unpack(mcc_pl, NULL, &error_code, &control_state);
	*fsm_state = control_state;
	*fsm_state_err = min_t(enum mlxfw_fsm_state_err, error_code, MLXFW_FSM_STATE_ERR_MAX);
	return 0;
}

static void mlxsw_core_fw_fsm_cancel(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_CANCEL, 0, fwhandle, 0);
	mlxsw_reg_write(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
}

static void mlxsw_core_fw_fsm_release(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlxsw_core_fw_info *mlxsw_core_fw_info =
		container_of(mlxfw_dev, struct mlxsw_core_fw_info, mlxfw_dev);
	struct mlxsw_core *mlxsw_core = mlxsw_core_fw_info->mlxsw_core;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_RELEASE_UPDATE_HANDLE, 0, fwhandle, 0);
	mlxsw_reg_write(mlxsw_core, MLXSW_REG(mcc), mcc_pl);
}

static const struct mlxfw_dev_ops mlxsw_core_fw_mlxsw_dev_ops = {
	.component_query	= mlxsw_core_fw_component_query,
	.fsm_lock		= mlxsw_core_fw_fsm_lock,
	.fsm_component_update	= mlxsw_core_fw_fsm_component_update,
	.fsm_block_download	= mlxsw_core_fw_fsm_block_download,
	.fsm_component_verify	= mlxsw_core_fw_fsm_component_verify,
	.fsm_activate		= mlxsw_core_fw_fsm_activate,
	.fsm_query_state	= mlxsw_core_fw_fsm_query_state,
	.fsm_cancel		= mlxsw_core_fw_fsm_cancel,
	.fsm_release		= mlxsw_core_fw_fsm_release,
};

static int mlxsw_core_dev_fw_flash(struct mlxsw_core *mlxsw_core,
				   const struct firmware *firmware,
				   struct netlink_ext_ack *extack)
{
	struct mlxsw_core_fw_info mlxsw_core_fw_info = {
		.mlxfw_dev = {
			.ops = &mlxsw_core_fw_mlxsw_dev_ops,
			.psid = mlxsw_core->bus_info->psid,
			.psid_size = strlen(mlxsw_core->bus_info->psid),
			.devlink = priv_to_devlink(mlxsw_core),
		},
		.mlxsw_core = mlxsw_core
	};

	return mlxsw_core_fw_flash(mlxsw_core, &mlxsw_core_fw_info.mlxfw_dev,
				   firmware, extack);
}

static int mlxsw_core_fw_rev_validate(struct mlxsw_core *mlxsw_core,
				      const struct mlxsw_bus_info *mlxsw_bus_info,
				      const struct mlxsw_fw_rev *req_rev,
				      const char *filename)
{
	const struct mlxsw_fw_rev *rev = &mlxsw_bus_info->fw_rev;
	union devlink_param_value value;
	const struct firmware *firmware;
	int err;

	/* Don't check if driver does not require it */
	if (!req_rev || !filename)
		return 0;

	/* Don't check if devlink 'fw_load_policy' param is 'flash' */
	err = devlink_param_driverinit_value_get(priv_to_devlink(mlxsw_core),
						 DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY,
						 &value);
	if (err)
		return err;
	if (value.vu8 == DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_FLASH)
		return 0;

	/* Validate driver & FW are compatible */
	if (rev->major != req_rev->major) {
		WARN(1, "Mismatch in major FW version [%d:%d] is never expected; Please contact support\n",
		     rev->major, req_rev->major);
		return -EINVAL;
	}
	if (mlxsw_core_fw_rev_minor_subminor_validate(rev, req_rev))
		return 0;

	dev_err(mlxsw_bus_info->dev, "The firmware version %d.%d.%d is incompatible with the driver (required >= %d.%d.%d)\n",
		rev->major, rev->minor, rev->subminor, req_rev->major,
		req_rev->minor, req_rev->subminor);
	dev_info(mlxsw_bus_info->dev, "Flashing firmware using file %s\n", filename);

	err = request_firmware_direct(&firmware, filename, mlxsw_bus_info->dev);
	if (err) {
		dev_err(mlxsw_bus_info->dev, "Could not request firmware file %s\n", filename);
		return err;
	}

	err = mlxsw_core_dev_fw_flash(mlxsw_core, firmware, NULL);
	release_firmware(firmware);
	if (err)
		dev_err(mlxsw_bus_info->dev, "Could not upgrade firmware\n");

	/* On FW flash success, tell the caller FW reset is needed
	 * if current FW supports it.
	 */
	if (rev->minor >= req_rev->can_reset_minor)
		return err ? err : -EAGAIN;
	else
		return 0;
}

static int mlxsw_core_fw_flash_update(struct mlxsw_core *mlxsw_core,
				      struct devlink_flash_update_params *params,
				      struct netlink_ext_ack *extack)
{
	return mlxsw_core_dev_fw_flash(mlxsw_core, params->fw, extack);
}

static int mlxsw_core_devlink_param_fw_load_policy_validate(struct devlink *devlink, u32 id,
							    union devlink_param_value val,
							    struct netlink_ext_ack *extack)
{
	if (val.vu8 != DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DRIVER &&
	    val.vu8 != DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_FLASH) {
		NL_SET_ERR_MSG_MOD(extack, "'fw_load_policy' must be 'driver' or 'flash'");
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param mlxsw_core_fw_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(FW_LOAD_POLICY, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT), NULL, NULL,
			      mlxsw_core_devlink_param_fw_load_policy_validate),
};

static int mlxsw_core_fw_params_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	union devlink_param_value value;
	int err;

	err = devlink_params_register(devlink, mlxsw_core_fw_devlink_params,
				      ARRAY_SIZE(mlxsw_core_fw_devlink_params));
	if (err)
		return err;

	value.vu8 = DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DRIVER;
	devlink_param_driverinit_value_set(devlink, DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY, value);
	return 0;
}

static void mlxsw_core_fw_params_unregister(struct mlxsw_core *mlxsw_core)
{
	devlink_params_unregister(priv_to_devlink(mlxsw_core), mlxsw_core_fw_devlink_params,
				  ARRAY_SIZE(mlxsw_core_fw_devlink_params));
}

static void *__dl_port(struct devlink_port *devlink_port)
{
	return container_of(devlink_port, struct mlxsw_core_port, devlink_port);
}

static int mlxsw_devlink_port_split(struct devlink *devlink,
				    struct devlink_port *port,
				    unsigned int count,
				    struct netlink_ext_ack *extack)
{
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(port);
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	if (!mlxsw_core->driver->port_split)
		return -EOPNOTSUPP;
	return mlxsw_core->driver->port_split(mlxsw_core,
					      mlxsw_core_port->local_port,
					      count, extack);
}

static int mlxsw_devlink_port_unsplit(struct devlink *devlink,
				      struct devlink_port *port,
				      struct netlink_ext_ack *extack)
{
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(port);
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	if (!mlxsw_core->driver->port_unsplit)
		return -EOPNOTSUPP;
	return mlxsw_core->driver->port_unsplit(mlxsw_core,
						mlxsw_core_port->local_port,
						extack);
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
			  enum devlink_sb_threshold_type threshold_type,
			  struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->sb_pool_set)
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_pool_set(mlxsw_core, sb_index,
					 pool_index, size, threshold_type,
					 extack);
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
					  u32 threshold,
					  struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_port_pool_set ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_port_pool_set(mlxsw_core_port, sb_index,
					      pool_index, threshold, extack);
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
				  u16 pool_index, u32 threshold,
				  struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink_port->devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;
	struct mlxsw_core_port *mlxsw_core_port = __dl_port(devlink_port);

	if (!mlxsw_driver->sb_tc_pool_bind_set ||
	    !mlxsw_core_port_check(mlxsw_core_port))
		return -EOPNOTSUPP;
	return mlxsw_driver->sb_tc_pool_bind_set(mlxsw_core_port, sb_index,
						 tc_index, pool_type,
						 pool_index, threshold, extack);
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

static int
mlxsw_devlink_info_get(struct devlink *devlink, struct devlink_info_req *req,
		       struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	char fw_info_psid[MLXSW_REG_MGIR_FW_INFO_PSID_SIZE];
	u32 hw_rev, fw_major, fw_minor, fw_sub_minor;
	char mgir_pl[MLXSW_REG_MGIR_LEN];
	char buf[32];
	int err;

	err = devlink_info_driver_name_put(req,
					   mlxsw_core->bus_info->device_kind);
	if (err)
		return err;

	mlxsw_reg_mgir_pack(mgir_pl);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mgir), mgir_pl);
	if (err)
		return err;
	mlxsw_reg_mgir_unpack(mgir_pl, &hw_rev, fw_info_psid, &fw_major,
			      &fw_minor, &fw_sub_minor);

	sprintf(buf, "%X", hw_rev);
	err = devlink_info_version_fixed_put(req, "hw.revision", buf);
	if (err)
		return err;

	err = devlink_info_version_fixed_put(req,
					     DEVLINK_INFO_VERSION_GENERIC_FW_PSID,
					     fw_info_psid);
	if (err)
		return err;

	sprintf(buf, "%d.%d.%d", fw_major, fw_minor, fw_sub_minor);
	err = devlink_info_version_running_put(req, "fw.version", buf);
	if (err)
		return err;

	return devlink_info_version_running_put(req,
						DEVLINK_INFO_VERSION_GENERIC_FW,
						buf);
}

static int
mlxsw_devlink_core_bus_device_reload_down(struct devlink *devlink,
					  bool netns_change, enum devlink_reload_action action,
					  enum devlink_reload_limit limit,
					  struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_RESET))
		return -EOPNOTSUPP;

	mlxsw_core_bus_device_unregister(mlxsw_core, true);
	return 0;
}

static int
mlxsw_devlink_core_bus_device_reload_up(struct devlink *devlink, enum devlink_reload_action action,
					enum devlink_reload_limit limit, u32 *actions_performed,
					struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	int err;

	*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT) |
			     BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE);
	err = mlxsw_core_bus_device_register(mlxsw_core->bus_info,
					     mlxsw_core->bus,
					     mlxsw_core->bus_priv, true,
					     devlink, extack);
	return err;
}

static int mlxsw_devlink_flash_update(struct devlink *devlink,
				      struct devlink_flash_update_params *params,
				      struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);

	return mlxsw_core_fw_flash_update(mlxsw_core, params, extack);
}

static int mlxsw_devlink_trap_init(struct devlink *devlink,
				   const struct devlink_trap *trap,
				   void *trap_ctx)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_init)
		return -EOPNOTSUPP;
	return mlxsw_driver->trap_init(mlxsw_core, trap, trap_ctx);
}

static void mlxsw_devlink_trap_fini(struct devlink *devlink,
				    const struct devlink_trap *trap,
				    void *trap_ctx)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_fini)
		return;
	mlxsw_driver->trap_fini(mlxsw_core, trap, trap_ctx);
}

static int mlxsw_devlink_trap_action_set(struct devlink *devlink,
					 const struct devlink_trap *trap,
					 enum devlink_trap_action action,
					 struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_action_set)
		return -EOPNOTSUPP;
	return mlxsw_driver->trap_action_set(mlxsw_core, trap, action, extack);
}

static int
mlxsw_devlink_trap_group_init(struct devlink *devlink,
			      const struct devlink_trap_group *group)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_group_init)
		return -EOPNOTSUPP;
	return mlxsw_driver->trap_group_init(mlxsw_core, group);
}

static int
mlxsw_devlink_trap_group_set(struct devlink *devlink,
			     const struct devlink_trap_group *group,
			     const struct devlink_trap_policer *policer,
			     struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_group_set)
		return -EOPNOTSUPP;
	return mlxsw_driver->trap_group_set(mlxsw_core, group, policer, extack);
}

static int
mlxsw_devlink_trap_policer_init(struct devlink *devlink,
				const struct devlink_trap_policer *policer)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_policer_init)
		return -EOPNOTSUPP;
	return mlxsw_driver->trap_policer_init(mlxsw_core, policer);
}

static void
mlxsw_devlink_trap_policer_fini(struct devlink *devlink,
				const struct devlink_trap_policer *policer)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_policer_fini)
		return;
	mlxsw_driver->trap_policer_fini(mlxsw_core, policer);
}

static int
mlxsw_devlink_trap_policer_set(struct devlink *devlink,
			       const struct devlink_trap_policer *policer,
			       u64 rate, u64 burst,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_policer_set)
		return -EOPNOTSUPP;
	return mlxsw_driver->trap_policer_set(mlxsw_core, policer, rate, burst,
					      extack);
}

static int
mlxsw_devlink_trap_policer_counter_get(struct devlink *devlink,
				       const struct devlink_trap_policer *policer,
				       u64 *p_drops)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_driver *mlxsw_driver = mlxsw_core->driver;

	if (!mlxsw_driver->trap_policer_counter_get)
		return -EOPNOTSUPP;
	return mlxsw_driver->trap_policer_counter_get(mlxsw_core, policer,
						      p_drops);
}

static const struct devlink_ops mlxsw_devlink_ops = {
	.reload_actions		= BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT) |
				  BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE),
	.reload_down		= mlxsw_devlink_core_bus_device_reload_down,
	.reload_up		= mlxsw_devlink_core_bus_device_reload_up,
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
	.info_get			= mlxsw_devlink_info_get,
	.flash_update			= mlxsw_devlink_flash_update,
	.trap_init			= mlxsw_devlink_trap_init,
	.trap_fini			= mlxsw_devlink_trap_fini,
	.trap_action_set		= mlxsw_devlink_trap_action_set,
	.trap_group_init		= mlxsw_devlink_trap_group_init,
	.trap_group_set			= mlxsw_devlink_trap_group_set,
	.trap_policer_init		= mlxsw_devlink_trap_policer_init,
	.trap_policer_fini		= mlxsw_devlink_trap_policer_fini,
	.trap_policer_set		= mlxsw_devlink_trap_policer_set,
	.trap_policer_counter_get	= mlxsw_devlink_trap_policer_counter_get,
};

static int mlxsw_core_params_register(struct mlxsw_core *mlxsw_core)
{
	int err;

	err = mlxsw_core_fw_params_register(mlxsw_core);
	if (err)
		return err;

	if (mlxsw_core->driver->params_register) {
		err = mlxsw_core->driver->params_register(mlxsw_core);
		if (err)
			goto err_params_register;
	}
	return 0;

err_params_register:
	mlxsw_core_fw_params_unregister(mlxsw_core);
	return err;
}

static void mlxsw_core_params_unregister(struct mlxsw_core *mlxsw_core)
{
	mlxsw_core_fw_params_unregister(mlxsw_core);
	if (mlxsw_core->driver->params_register)
		mlxsw_core->driver->params_unregister(mlxsw_core);
}

struct mlxsw_core_health_event {
	struct mlxsw_core *mlxsw_core;
	char mfde_pl[MLXSW_REG_MFDE_LEN];
	struct work_struct work;
};

static void mlxsw_core_health_event_work(struct work_struct *work)
{
	struct mlxsw_core_health_event *event;
	struct mlxsw_core *mlxsw_core;

	event = container_of(work, struct mlxsw_core_health_event, work);
	mlxsw_core = event->mlxsw_core;
	devlink_health_report(mlxsw_core->health.fw_fatal, "FW fatal event occurred",
			      event->mfde_pl);
	kfree(event);
}

static void mlxsw_core_health_listener_func(const struct mlxsw_reg_info *reg,
					    char *mfde_pl, void *priv)
{
	struct mlxsw_core_health_event *event;
	struct mlxsw_core *mlxsw_core = priv;

	event = kmalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return;
	event->mlxsw_core = mlxsw_core;
	memcpy(event->mfde_pl, mfde_pl, sizeof(event->mfde_pl));
	INIT_WORK(&event->work, mlxsw_core_health_event_work);
	mlxsw_core_schedule_work(&event->work);
}

static const struct mlxsw_listener mlxsw_core_health_listener =
	MLXSW_CORE_EVENTL(mlxsw_core_health_listener_func, MFDE);

static int
mlxsw_core_health_fw_fatal_dump_fatal_cause(const char *mfde_pl,
					    struct devlink_fmsg *fmsg)
{
	u32 val, tile_v;
	int err;

	val = mlxsw_reg_mfde_fatal_cause_id_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "cause_id", val);
	if (err)
		return err;
	tile_v = mlxsw_reg_mfde_fatal_cause_tile_v_get(mfde_pl);
	if (tile_v) {
		val = mlxsw_reg_mfde_fatal_cause_tile_index_get(mfde_pl);
		err = devlink_fmsg_u8_pair_put(fmsg, "tile_index", val);
		if (err)
			return err;
	}

	return 0;
}

static int
mlxsw_core_health_fw_fatal_dump_fw_assert(const char *mfde_pl,
					  struct devlink_fmsg *fmsg)
{
	u32 val, tile_v;
	int err;

	val = mlxsw_reg_mfde_fw_assert_var0_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "var0", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_fw_assert_var1_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "var1", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_fw_assert_var2_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "var2", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_fw_assert_var3_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "var3", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_fw_assert_var4_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "var4", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_fw_assert_existptr_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "existptr", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_fw_assert_callra_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "callra", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_fw_assert_oe_get(mfde_pl);
	err = devlink_fmsg_bool_pair_put(fmsg, "old_event", val);
	if (err)
		return err;
	tile_v = mlxsw_reg_mfde_fw_assert_tile_v_get(mfde_pl);
	if (tile_v) {
		val = mlxsw_reg_mfde_fw_assert_tile_index_get(mfde_pl);
		err = devlink_fmsg_u8_pair_put(fmsg, "tile_index", val);
		if (err)
			return err;
	}
	val = mlxsw_reg_mfde_fw_assert_ext_synd_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "ext_synd", val);
	if (err)
		return err;

	return 0;
}

static int
mlxsw_core_health_fw_fatal_dump_kvd_im_stop(const char *mfde_pl,
					    struct devlink_fmsg *fmsg)
{
	u32 val;
	int err;

	val = mlxsw_reg_mfde_kvd_im_stop_oe_get(mfde_pl);
	err = devlink_fmsg_bool_pair_put(fmsg, "old_event", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_kvd_im_stop_pipes_mask_get(mfde_pl);
	return devlink_fmsg_u32_pair_put(fmsg, "pipes_mask", val);
}

static int
mlxsw_core_health_fw_fatal_dump_crspace_to(const char *mfde_pl,
					   struct devlink_fmsg *fmsg)
{
	u32 val;
	int err;

	val = mlxsw_reg_mfde_crspace_to_log_address_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "log_address", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_crspace_to_oe_get(mfde_pl);
	err = devlink_fmsg_bool_pair_put(fmsg, "old_event", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_crspace_to_log_id_get(mfde_pl);
	err = devlink_fmsg_u8_pair_put(fmsg, "log_irisc_id", val);
	if (err)
		return err;
	val = mlxsw_reg_mfde_crspace_to_log_ip_get(mfde_pl);
	err = devlink_fmsg_u64_pair_put(fmsg, "log_ip", val);
	if (err)
		return err;

	return 0;
}

static int mlxsw_core_health_fw_fatal_dump(struct devlink_health_reporter *reporter,
					   struct devlink_fmsg *fmsg, void *priv_ctx,
					   struct netlink_ext_ack *extack)
{
	char *mfde_pl = priv_ctx;
	char *val_str;
	u8 event_id;
	u32 val;
	int err;

	if (!priv_ctx)
		/* User-triggered dumps are not possible */
		return -EOPNOTSUPP;

	val = mlxsw_reg_mfde_irisc_id_get(mfde_pl);
	err = devlink_fmsg_u8_pair_put(fmsg, "irisc_id", val);
	if (err)
		return err;
	err = devlink_fmsg_arr_pair_nest_start(fmsg, "event");
	if (err)
		return err;

	event_id = mlxsw_reg_mfde_event_id_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "id", event_id);
	if (err)
		return err;
	switch (event_id) {
	case MLXSW_REG_MFDE_EVENT_ID_CRSPACE_TO:
		val_str = "CR space timeout";
		break;
	case MLXSW_REG_MFDE_EVENT_ID_KVD_IM_STOP:
		val_str = "KVD insertion machine stopped";
		break;
	case MLXSW_REG_MFDE_EVENT_ID_TEST:
		val_str = "Test";
		break;
	case MLXSW_REG_MFDE_EVENT_ID_FW_ASSERT:
		val_str = "FW assert";
		break;
	case MLXSW_REG_MFDE_EVENT_ID_FATAL_CAUSE:
		val_str = "Fatal cause";
		break;
	default:
		val_str = NULL;
	}
	if (val_str) {
		err = devlink_fmsg_string_pair_put(fmsg, "desc", val_str);
		if (err)
			return err;
	}

	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "severity");
	if (err)
		return err;

	val = mlxsw_reg_mfde_severity_get(mfde_pl);
	err = devlink_fmsg_u8_pair_put(fmsg, "id", val);
	if (err)
		return err;
	switch (val) {
	case MLXSW_REG_MFDE_SEVERITY_FATL:
		val_str = "Fatal";
		break;
	case MLXSW_REG_MFDE_SEVERITY_NRML:
		val_str = "Normal";
		break;
	case MLXSW_REG_MFDE_SEVERITY_INTR:
		val_str = "Debug";
		break;
	default:
		val_str = NULL;
	}
	if (val_str) {
		err = devlink_fmsg_string_pair_put(fmsg, "desc", val_str);
		if (err)
			return err;
	}

	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;

	val = mlxsw_reg_mfde_method_get(mfde_pl);
	switch (val) {
	case MLXSW_REG_MFDE_METHOD_QUERY:
		val_str = "query";
		break;
	case MLXSW_REG_MFDE_METHOD_WRITE:
		val_str = "write";
		break;
	default:
		val_str = NULL;
	}
	if (val_str) {
		err = devlink_fmsg_string_pair_put(fmsg, "method", val_str);
		if (err)
			return err;
	}

	val = mlxsw_reg_mfde_long_process_get(mfde_pl);
	err = devlink_fmsg_bool_pair_put(fmsg, "long_process", val);
	if (err)
		return err;

	val = mlxsw_reg_mfde_command_type_get(mfde_pl);
	switch (val) {
	case MLXSW_REG_MFDE_COMMAND_TYPE_MAD:
		val_str = "mad";
		break;
	case MLXSW_REG_MFDE_COMMAND_TYPE_EMAD:
		val_str = "emad";
		break;
	case MLXSW_REG_MFDE_COMMAND_TYPE_CMDIF:
		val_str = "cmdif";
		break;
	default:
		val_str = NULL;
	}
	if (val_str) {
		err = devlink_fmsg_string_pair_put(fmsg, "command_type", val_str);
		if (err)
			return err;
	}

	val = mlxsw_reg_mfde_reg_attr_id_get(mfde_pl);
	err = devlink_fmsg_u32_pair_put(fmsg, "reg_attr_id", val);
	if (err)
		return err;

	switch (event_id) {
	case MLXSW_REG_MFDE_EVENT_ID_CRSPACE_TO:
		return mlxsw_core_health_fw_fatal_dump_crspace_to(mfde_pl,
								  fmsg);
	case MLXSW_REG_MFDE_EVENT_ID_KVD_IM_STOP:
		return mlxsw_core_health_fw_fatal_dump_kvd_im_stop(mfde_pl,
								   fmsg);
	case MLXSW_REG_MFDE_EVENT_ID_FW_ASSERT:
		return mlxsw_core_health_fw_fatal_dump_fw_assert(mfde_pl, fmsg);
	case MLXSW_REG_MFDE_EVENT_ID_FATAL_CAUSE:
		return mlxsw_core_health_fw_fatal_dump_fatal_cause(mfde_pl,
								   fmsg);
	}

	return 0;
}

static int
mlxsw_core_health_fw_fatal_test(struct devlink_health_reporter *reporter,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_core *mlxsw_core = devlink_health_reporter_priv(reporter);
	char mfgd_pl[MLXSW_REG_MFGD_LEN];
	int err;

	/* Read the register first to make sure no other bits are changed. */
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mfgd), mfgd_pl);
	if (err)
		return err;
	mlxsw_reg_mfgd_trigger_test_set(mfgd_pl, true);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mfgd), mfgd_pl);
}

static const struct devlink_health_reporter_ops
mlxsw_core_health_fw_fatal_ops = {
	.name = "fw_fatal",
	.dump = mlxsw_core_health_fw_fatal_dump,
	.test = mlxsw_core_health_fw_fatal_test,
};

static int mlxsw_core_health_fw_fatal_config(struct mlxsw_core *mlxsw_core,
					     bool enable)
{
	char mfgd_pl[MLXSW_REG_MFGD_LEN];
	int err;

	/* Read the register first to make sure no other bits are changed. */
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mfgd), mfgd_pl);
	if (err)
		return err;
	mlxsw_reg_mfgd_fatal_event_mode_set(mfgd_pl, enable);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mfgd), mfgd_pl);
}

static int mlxsw_core_health_init(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct devlink_health_reporter *fw_fatal;
	int err;

	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_TXRX))
		return 0;

	fw_fatal = devlink_health_reporter_create(devlink, &mlxsw_core_health_fw_fatal_ops,
						  0, mlxsw_core);
	if (IS_ERR(fw_fatal)) {
		dev_err(mlxsw_core->bus_info->dev, "Failed to create fw fatal reporter");
		return PTR_ERR(fw_fatal);
	}
	mlxsw_core->health.fw_fatal = fw_fatal;

	err = mlxsw_core_trap_register(mlxsw_core, &mlxsw_core_health_listener, mlxsw_core);
	if (err)
		goto err_trap_register;

	err = mlxsw_core_health_fw_fatal_config(mlxsw_core, true);
	if (err)
		goto err_fw_fatal_config;

	return 0;

err_fw_fatal_config:
	mlxsw_core_trap_unregister(mlxsw_core, &mlxsw_core_health_listener, mlxsw_core);
err_trap_register:
	devlink_health_reporter_destroy(mlxsw_core->health.fw_fatal);
	return err;
}

static void mlxsw_core_health_fini(struct mlxsw_core *mlxsw_core)
{
	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_TXRX))
		return;

	mlxsw_core_health_fw_fatal_config(mlxsw_core, false);
	mlxsw_core_trap_unregister(mlxsw_core, &mlxsw_core_health_listener, mlxsw_core);
	/* Make sure there is no more event work scheduled */
	mlxsw_core_flush_owq();
	devlink_health_reporter_destroy(mlxsw_core->health.fw_fatal);
}

static int
__mlxsw_core_bus_device_register(const struct mlxsw_bus_info *mlxsw_bus_info,
				 const struct mlxsw_bus *mlxsw_bus,
				 void *bus_priv, bool reload,
				 struct devlink *devlink,
				 struct netlink_ext_ack *extack)
{
	const char *device_kind = mlxsw_bus_info->device_kind;
	struct mlxsw_core *mlxsw_core;
	struct mlxsw_driver *mlxsw_driver;
	size_t alloc_size;
	int err;

	mlxsw_driver = mlxsw_core_driver_get(device_kind);
	if (!mlxsw_driver)
		return -EINVAL;

	if (!reload) {
		alloc_size = sizeof(*mlxsw_core) + mlxsw_driver->priv_size;
		devlink = devlink_alloc(&mlxsw_devlink_ops, alloc_size,
					mlxsw_bus_info->dev);
		if (!devlink) {
			err = -ENOMEM;
			goto err_devlink_alloc;
		}
		devl_lock(devlink);
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

	if (mlxsw_driver->resources_register && !reload) {
		err = mlxsw_driver->resources_register(mlxsw_core);
		if (err)
			goto err_register_resources;
	}

	err = mlxsw_ports_init(mlxsw_core, reload);
	if (err)
		goto err_ports_init;

	if (MLXSW_CORE_RES_VALID(mlxsw_core, MAX_LAG) &&
	    MLXSW_CORE_RES_VALID(mlxsw_core, MAX_LAG_MEMBERS)) {
		alloc_size = sizeof(*mlxsw_core->lag.mapping) *
			MLXSW_CORE_RES_GET(mlxsw_core, MAX_LAG) *
			MLXSW_CORE_RES_GET(mlxsw_core, MAX_LAG_MEMBERS);
		mlxsw_core->lag.mapping = kzalloc(alloc_size, GFP_KERNEL);
		if (!mlxsw_core->lag.mapping) {
			err = -ENOMEM;
			goto err_alloc_lag_mapping;
		}
	}

	err = mlxsw_core_trap_groups_set(mlxsw_core);
	if (err)
		goto err_trap_groups_set;

	err = mlxsw_emad_init(mlxsw_core);
	if (err)
		goto err_emad_init;

	if (!reload) {
		err = mlxsw_core_params_register(mlxsw_core);
		if (err)
			goto err_register_params;
	}

	err = mlxsw_core_fw_rev_validate(mlxsw_core, mlxsw_bus_info, mlxsw_driver->fw_req_rev,
					 mlxsw_driver->fw_filename);
	if (err)
		goto err_fw_rev_validate;

	err = mlxsw_linecards_init(mlxsw_core, mlxsw_bus_info);
	if (err)
		goto err_linecards_init;

	err = mlxsw_core_health_init(mlxsw_core);
	if (err)
		goto err_health_init;

	err = mlxsw_hwmon_init(mlxsw_core, mlxsw_bus_info, &mlxsw_core->hwmon);
	if (err)
		goto err_hwmon_init;

	err = mlxsw_thermal_init(mlxsw_core, mlxsw_bus_info,
				 &mlxsw_core->thermal);
	if (err)
		goto err_thermal_init;

	err = mlxsw_env_init(mlxsw_core, mlxsw_bus_info, &mlxsw_core->env);
	if (err)
		goto err_env_init;

	if (mlxsw_driver->init) {
		err = mlxsw_driver->init(mlxsw_core, mlxsw_bus_info, extack);
		if (err)
			goto err_driver_init;
	}

	if (!reload) {
		devlink_set_features(devlink, DEVLINK_F_RELOAD);
		devl_unlock(devlink);
		devlink_register(devlink);
	}
	return 0;

err_driver_init:
	mlxsw_env_fini(mlxsw_core->env);
err_env_init:
	mlxsw_thermal_fini(mlxsw_core->thermal);
err_thermal_init:
	mlxsw_hwmon_fini(mlxsw_core->hwmon);
err_hwmon_init:
	mlxsw_core_health_fini(mlxsw_core);
err_health_init:
	mlxsw_linecards_fini(mlxsw_core);
err_linecards_init:
err_fw_rev_validate:
	if (!reload)
		mlxsw_core_params_unregister(mlxsw_core);
err_register_params:
	mlxsw_emad_fini(mlxsw_core);
err_emad_init:
err_trap_groups_set:
	kfree(mlxsw_core->lag.mapping);
err_alloc_lag_mapping:
	mlxsw_ports_fini(mlxsw_core, reload);
err_ports_init:
	if (!reload)
		devl_resources_unregister(devlink);
err_register_resources:
	mlxsw_bus->fini(bus_priv);
err_bus_init:
	if (!reload) {
		devl_unlock(devlink);
		devlink_free(devlink);
	}
err_devlink_alloc:
	return err;
}

int mlxsw_core_bus_device_register(const struct mlxsw_bus_info *mlxsw_bus_info,
				   const struct mlxsw_bus *mlxsw_bus,
				   void *bus_priv, bool reload,
				   struct devlink *devlink,
				   struct netlink_ext_ack *extack)
{
	bool called_again = false;
	int err;

again:
	err = __mlxsw_core_bus_device_register(mlxsw_bus_info, mlxsw_bus,
					       bus_priv, reload,
					       devlink, extack);
	/* -EAGAIN is returned in case the FW was updated. FW needs
	 * a reset, so lets try to call __mlxsw_core_bus_device_register()
	 * again.
	 */
	if (err == -EAGAIN && !called_again) {
		called_again = true;
		goto again;
	}

	return err;
}
EXPORT_SYMBOL(mlxsw_core_bus_device_register);

void mlxsw_core_bus_device_unregister(struct mlxsw_core *mlxsw_core,
				      bool reload)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);

	if (!reload) {
		devlink_unregister(devlink);
		devl_lock(devlink);
	}

	if (devlink_is_reload_failed(devlink)) {
		if (!reload)
			/* Only the parts that were not de-initialized in the
			 * failed reload attempt need to be de-initialized.
			 */
			goto reload_fail_deinit;
		else
			return;
	}

	if (mlxsw_core->driver->fini)
		mlxsw_core->driver->fini(mlxsw_core);
	mlxsw_env_fini(mlxsw_core->env);
	mlxsw_thermal_fini(mlxsw_core->thermal);
	mlxsw_hwmon_fini(mlxsw_core->hwmon);
	mlxsw_core_health_fini(mlxsw_core);
	mlxsw_linecards_fini(mlxsw_core);
	if (!reload)
		mlxsw_core_params_unregister(mlxsw_core);
	mlxsw_emad_fini(mlxsw_core);
	kfree(mlxsw_core->lag.mapping);
	mlxsw_ports_fini(mlxsw_core, reload);
	if (!reload)
		devl_resources_unregister(devlink);
	mlxsw_core->bus->fini(mlxsw_core->bus_priv);
	if (!reload) {
		devl_unlock(devlink);
		devlink_free(devlink);
	}

	return;

reload_fail_deinit:
	mlxsw_core_params_unregister(mlxsw_core);
	devl_resources_unregister(devlink);
	devl_unlock(devlink);
	devlink_free(devlink);
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

void mlxsw_core_ptp_transmitted(struct mlxsw_core *mlxsw_core,
				struct sk_buff *skb, u16 local_port)
{
	if (mlxsw_core->driver->ptp_transmitted)
		mlxsw_core->driver->ptp_transmitted(mlxsw_core, skb,
						    local_port);
}
EXPORT_SYMBOL(mlxsw_core_ptp_transmitted);

static bool __is_rx_listener_equal(const struct mlxsw_rx_listener *rxl_a,
				   const struct mlxsw_rx_listener *rxl_b)
{
	return (rxl_a->func == rxl_b->func &&
		rxl_a->local_port == rxl_b->local_port &&
		rxl_a->trap_id == rxl_b->trap_id &&
		rxl_a->mirror_reason == rxl_b->mirror_reason);
}

static struct mlxsw_rx_listener_item *
__find_rx_listener_item(struct mlxsw_core *mlxsw_core,
			const struct mlxsw_rx_listener *rxl)
{
	struct mlxsw_rx_listener_item *rxl_item;

	list_for_each_entry(rxl_item, &mlxsw_core->rx_listener_list, list) {
		if (__is_rx_listener_equal(&rxl_item->rxl, rxl))
			return rxl_item;
	}
	return NULL;
}

int mlxsw_core_rx_listener_register(struct mlxsw_core *mlxsw_core,
				    const struct mlxsw_rx_listener *rxl,
				    void *priv, bool enabled)
{
	struct mlxsw_rx_listener_item *rxl_item;

	rxl_item = __find_rx_listener_item(mlxsw_core, rxl);
	if (rxl_item)
		return -EEXIST;
	rxl_item = kmalloc(sizeof(*rxl_item), GFP_KERNEL);
	if (!rxl_item)
		return -ENOMEM;
	rxl_item->rxl = *rxl;
	rxl_item->priv = priv;
	rxl_item->enabled = enabled;

	list_add_rcu(&rxl_item->list, &mlxsw_core->rx_listener_list);
	return 0;
}
EXPORT_SYMBOL(mlxsw_core_rx_listener_register);

void mlxsw_core_rx_listener_unregister(struct mlxsw_core *mlxsw_core,
				       const struct mlxsw_rx_listener *rxl)
{
	struct mlxsw_rx_listener_item *rxl_item;

	rxl_item = __find_rx_listener_item(mlxsw_core, rxl);
	if (!rxl_item)
		return;
	list_del_rcu(&rxl_item->list);
	synchronize_rcu();
	kfree(rxl_item);
}
EXPORT_SYMBOL(mlxsw_core_rx_listener_unregister);

static void
mlxsw_core_rx_listener_state_set(struct mlxsw_core *mlxsw_core,
				 const struct mlxsw_rx_listener *rxl,
				 bool enabled)
{
	struct mlxsw_rx_listener_item *rxl_item;

	rxl_item = __find_rx_listener_item(mlxsw_core, rxl);
	if (WARN_ON(!rxl_item))
		return;
	rxl_item->enabled = enabled;
}

static void mlxsw_core_event_listener_func(struct sk_buff *skb, u16 local_port,
					   void *priv)
{
	struct mlxsw_event_listener_item *event_listener_item = priv;
	struct mlxsw_core *mlxsw_core;
	struct mlxsw_reg_info reg;
	char *payload;
	char *reg_tlv;
	char *op_tlv;

	mlxsw_core = event_listener_item->mlxsw_core;
	trace_devlink_hwmsg(priv_to_devlink(mlxsw_core), true, 0,
			    skb->data, skb->len);

	mlxsw_emad_tlv_parse(skb);
	op_tlv = mlxsw_emad_op_tlv(skb);
	reg_tlv = mlxsw_emad_reg_tlv(skb);

	reg.id = mlxsw_emad_op_tlv_register_id_get(op_tlv);
	reg.len = (mlxsw_emad_reg_tlv_len_get(reg_tlv) - 1) * sizeof(u32);
	payload = mlxsw_emad_reg_payload(reg_tlv);
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
			   const struct mlxsw_event_listener *el)
{
	struct mlxsw_event_listener_item *el_item;

	list_for_each_entry(el_item, &mlxsw_core->event_listener_list, list) {
		if (__is_event_listener_equal(&el_item->el, el))
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

	el_item = __find_event_listener_item(mlxsw_core, el);
	if (el_item)
		return -EEXIST;
	el_item = kmalloc(sizeof(*el_item), GFP_KERNEL);
	if (!el_item)
		return -ENOMEM;
	el_item->mlxsw_core = mlxsw_core;
	el_item->el = *el;
	el_item->priv = priv;

	err = mlxsw_core_rx_listener_register(mlxsw_core, &rxl, el_item, true);
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
					  const struct mlxsw_event_listener *el)
{
	struct mlxsw_event_listener_item *el_item;
	const struct mlxsw_rx_listener rxl = {
		.func = mlxsw_core_event_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = el->trap_id,
	};

	el_item = __find_event_listener_item(mlxsw_core, el);
	if (!el_item)
		return;
	mlxsw_core_rx_listener_unregister(mlxsw_core, &rxl);
	list_del(&el_item->list);
	kfree(el_item);
}
EXPORT_SYMBOL(mlxsw_core_event_listener_unregister);

static int mlxsw_core_listener_register(struct mlxsw_core *mlxsw_core,
					const struct mlxsw_listener *listener,
					void *priv, bool enabled)
{
	if (listener->is_event) {
		WARN_ON(!enabled);
		return mlxsw_core_event_listener_register(mlxsw_core,
						&listener->event_listener,
						priv);
	} else {
		return mlxsw_core_rx_listener_register(mlxsw_core,
						&listener->rx_listener,
						priv, enabled);
	}
}

static void mlxsw_core_listener_unregister(struct mlxsw_core *mlxsw_core,
				      const struct mlxsw_listener *listener,
				      void *priv)
{
	if (listener->is_event)
		mlxsw_core_event_listener_unregister(mlxsw_core,
						     &listener->event_listener);
	else
		mlxsw_core_rx_listener_unregister(mlxsw_core,
						  &listener->rx_listener);
}

int mlxsw_core_trap_register(struct mlxsw_core *mlxsw_core,
			     const struct mlxsw_listener *listener, void *priv)
{
	enum mlxsw_reg_htgt_trap_group trap_group;
	enum mlxsw_reg_hpkt_action action;
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int err;

	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_TXRX))
		return 0;

	err = mlxsw_core_listener_register(mlxsw_core, listener, priv,
					   listener->enabled_on_register);
	if (err)
		return err;

	action = listener->enabled_on_register ? listener->en_action :
						 listener->dis_action;
	trap_group = listener->enabled_on_register ? listener->en_trap_group :
						     listener->dis_trap_group;
	mlxsw_reg_hpkt_pack(hpkt_pl, action, listener->trap_id,
			    trap_group, listener->is_ctrl);
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

	if (!(mlxsw_core->bus->features & MLXSW_BUS_F_TXRX))
		return;

	if (!listener->is_event) {
		mlxsw_reg_hpkt_pack(hpkt_pl, listener->dis_action,
				    listener->trap_id, listener->dis_trap_group,
				    listener->is_ctrl);
		mlxsw_reg_write(mlxsw_core, MLXSW_REG(hpkt), hpkt_pl);
	}

	mlxsw_core_listener_unregister(mlxsw_core, listener, priv);
}
EXPORT_SYMBOL(mlxsw_core_trap_unregister);

int mlxsw_core_traps_register(struct mlxsw_core *mlxsw_core,
			      const struct mlxsw_listener *listeners,
			      size_t listeners_count, void *priv)
{
	int i, err;

	for (i = 0; i < listeners_count; i++) {
		err = mlxsw_core_trap_register(mlxsw_core,
					       &listeners[i],
					       priv);
		if (err)
			goto err_listener_register;
	}
	return 0;

err_listener_register:
	for (i--; i >= 0; i--) {
		mlxsw_core_trap_unregister(mlxsw_core,
					   &listeners[i],
					   priv);
	}
	return err;
}
EXPORT_SYMBOL(mlxsw_core_traps_register);

void mlxsw_core_traps_unregister(struct mlxsw_core *mlxsw_core,
				 const struct mlxsw_listener *listeners,
				 size_t listeners_count, void *priv)
{
	int i;

	for (i = 0; i < listeners_count; i++) {
		mlxsw_core_trap_unregister(mlxsw_core,
					   &listeners[i],
					   priv);
	}
}
EXPORT_SYMBOL(mlxsw_core_traps_unregister);

int mlxsw_core_trap_state_set(struct mlxsw_core *mlxsw_core,
			      const struct mlxsw_listener *listener,
			      bool enabled)
{
	enum mlxsw_reg_htgt_trap_group trap_group;
	enum mlxsw_reg_hpkt_action action;
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int err;

	/* Not supported for event listener */
	if (WARN_ON(listener->is_event))
		return -EINVAL;

	action = enabled ? listener->en_action : listener->dis_action;
	trap_group = enabled ? listener->en_trap_group :
			       listener->dis_trap_group;
	mlxsw_reg_hpkt_pack(hpkt_pl, action, listener->trap_id,
			    trap_group, listener->is_ctrl);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(hpkt), hpkt_pl);
	if (err)
		return err;

	mlxsw_core_rx_listener_state_set(mlxsw_core, &listener->rx_listener,
					 enabled);
	return 0;
}
EXPORT_SYMBOL(mlxsw_core_trap_state_set);

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
		kfree_rcu(trans, rcu);
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

#define MLXSW_REG_TRANS_ERR_STRING_SIZE	256

static int mlxsw_reg_trans_wait(struct mlxsw_reg_trans *trans)
{
	char err_string[MLXSW_REG_TRANS_ERR_STRING_SIZE];
	struct mlxsw_core *mlxsw_core = trans->core;
	int err;

	wait_for_completion(&trans->completion);
	cancel_delayed_work_sync(&trans->timeout_dw);
	err = trans->err;

	if (trans->retries)
		dev_warn(mlxsw_core->bus_info->dev, "EMAD retries (%d/%d) (tid=%llx)\n",
			 trans->retries, MLXSW_EMAD_MAX_RETRY, trans->tid);
	if (err) {
		dev_err(mlxsw_core->bus_info->dev, "EMAD reg access failed (tid=%llx,reg_id=%x(%s),type=%s,status=%x(%s))\n",
			trans->tid, trans->reg->id,
			mlxsw_reg_id_str(trans->reg->id),
			mlxsw_core_reg_access_type_str(trans->type),
			trans->emad_status,
			mlxsw_emad_op_tlv_status_str(trans->emad_status));

		snprintf(err_string, MLXSW_REG_TRANS_ERR_STRING_SIZE,
			 "(tid=%llx,reg_id=%x(%s)) %s (%s)\n", trans->tid,
			 trans->reg->id, mlxsw_reg_id_str(trans->reg->id),
			 mlxsw_emad_op_tlv_status_str(trans->emad_status),
			 trans->emad_err_string ? trans->emad_err_string : "");

		trace_devlink_hwerr(priv_to_devlink(mlxsw_core),
				    trans->emad_status, err_string);

		kfree(trans->emad_err_string);
	}

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
	bool reset_ok;
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

	/* There is a special treatment needed for MRSR (reset) register.
	 * The command interface will return error after the command
	 * is executed, so tell the lower layer to expect it
	 * and cope accordingly.
	 */
	reset_ok = reg->id == MLXSW_REG_MRSR_ID;

	n_retry = 0;
retry:
	err = mlxsw_cmd_access_reg(mlxsw_core, reset_ok, in_mbox, out_mbox);
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
		memcpy(payload, mlxsw_emad_reg_payload_cmd(out_mbox),
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
	u16 local_port;
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
		    rxl->trap_id == rx_info->trap_id &&
		    rxl->mirror_reason == rx_info->mirror_reason) {
			if (rxl_item->enabled)
				found = true;
			break;
		}
	}
	if (!found) {
		rcu_read_unlock();
		goto drop;
	}

	rxl->func(skb, local_port, rxl_item->priv);
	rcu_read_unlock();
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
				u16 lag_id, u8 port_index, u16 local_port)
{
	int index = mlxsw_core_lag_mapping_index(mlxsw_core,
						 lag_id, port_index);

	mlxsw_core->lag.mapping[index] = local_port;
}
EXPORT_SYMBOL(mlxsw_core_lag_mapping_set);

u16 mlxsw_core_lag_mapping_get(struct mlxsw_core *mlxsw_core,
			       u16 lag_id, u8 port_index)
{
	int index = mlxsw_core_lag_mapping_index(mlxsw_core,
						 lag_id, port_index);

	return mlxsw_core->lag.mapping[index];
}
EXPORT_SYMBOL(mlxsw_core_lag_mapping_get);

void mlxsw_core_lag_mapping_clear(struct mlxsw_core *mlxsw_core,
				  u16 lag_id, u16 local_port)
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

static int __mlxsw_core_port_init(struct mlxsw_core *mlxsw_core, u16 local_port,
				  enum devlink_port_flavour flavour,
				  u8 slot_index, u32 port_number, bool split,
				  u32 split_port_subnumber,
				  bool splittable, u32 lanes,
				  const unsigned char *switch_id,
				  unsigned char switch_id_len)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.split = split;
	attrs.lanes = lanes;
	attrs.splittable = splittable;
	attrs.flavour = flavour;
	attrs.phys.port_number = port_number;
	attrs.phys.split_subport_number = split_port_subnumber;
	memcpy(attrs.switch_id.id, switch_id, switch_id_len);
	attrs.switch_id.id_len = switch_id_len;
	mlxsw_core_port->local_port = local_port;
	devlink_port_attrs_set(devlink_port, &attrs);
	if (slot_index) {
		struct mlxsw_linecard *linecard;

		linecard = mlxsw_linecard_get(mlxsw_core->linecards,
					      slot_index);
		mlxsw_core_port->linecard = linecard;
		devlink_port_linecard_set(devlink_port,
					  linecard->devlink_linecard);
	}
	err = devl_port_register(devlink, devlink_port, local_port);
	if (err)
		memset(mlxsw_core_port, 0, sizeof(*mlxsw_core_port));
	return err;
}

static void __mlxsw_core_port_fini(struct mlxsw_core *mlxsw_core, u16 local_port)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	devl_port_unregister(devlink_port);
	memset(mlxsw_core_port, 0, sizeof(*mlxsw_core_port));
}

int mlxsw_core_port_init(struct mlxsw_core *mlxsw_core, u16 local_port,
			 u8 slot_index, u32 port_number, bool split,
			 u32 split_port_subnumber,
			 bool splittable, u32 lanes,
			 const unsigned char *switch_id,
			 unsigned char switch_id_len)
{
	int err;

	err = __mlxsw_core_port_init(mlxsw_core, local_port,
				     DEVLINK_PORT_FLAVOUR_PHYSICAL, slot_index,
				     port_number, split, split_port_subnumber,
				     splittable, lanes,
				     switch_id, switch_id_len);
	if (err)
		return err;

	atomic_inc(&mlxsw_core->active_ports_count);
	return 0;
}
EXPORT_SYMBOL(mlxsw_core_port_init);

void mlxsw_core_port_fini(struct mlxsw_core *mlxsw_core, u16 local_port)
{
	atomic_dec(&mlxsw_core->active_ports_count);

	__mlxsw_core_port_fini(mlxsw_core, local_port);
}
EXPORT_SYMBOL(mlxsw_core_port_fini);

int mlxsw_core_cpu_port_init(struct mlxsw_core *mlxsw_core,
			     void *port_driver_priv,
			     const unsigned char *switch_id,
			     unsigned char switch_id_len)
{
	struct mlxsw_core_port *mlxsw_core_port =
				&mlxsw_core->ports[MLXSW_PORT_CPU_PORT];
	int err;

	err = __mlxsw_core_port_init(mlxsw_core, MLXSW_PORT_CPU_PORT,
				     DEVLINK_PORT_FLAVOUR_CPU,
				     0, 0, false, 0, false, 0,
				     switch_id, switch_id_len);
	if (err)
		return err;

	mlxsw_core_port->port_driver_priv = port_driver_priv;
	return 0;
}
EXPORT_SYMBOL(mlxsw_core_cpu_port_init);

void mlxsw_core_cpu_port_fini(struct mlxsw_core *mlxsw_core)
{
	__mlxsw_core_port_fini(mlxsw_core, MLXSW_PORT_CPU_PORT);
}
EXPORT_SYMBOL(mlxsw_core_cpu_port_fini);

void mlxsw_core_port_eth_set(struct mlxsw_core *mlxsw_core, u16 local_port,
			     void *port_driver_priv, struct net_device *dev)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	mlxsw_core_port->port_driver_priv = port_driver_priv;
	devlink_port_type_eth_set(devlink_port, dev);
}
EXPORT_SYMBOL(mlxsw_core_port_eth_set);

void mlxsw_core_port_ib_set(struct mlxsw_core *mlxsw_core, u16 local_port,
			    void *port_driver_priv)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	mlxsw_core_port->port_driver_priv = port_driver_priv;
	devlink_port_type_ib_set(devlink_port, NULL);
}
EXPORT_SYMBOL(mlxsw_core_port_ib_set);

void mlxsw_core_port_clear(struct mlxsw_core *mlxsw_core, u16 local_port,
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
						u16 local_port)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	return devlink_port->type;
}
EXPORT_SYMBOL(mlxsw_core_port_type_get);


struct devlink_port *
mlxsw_core_port_devlink_port_get(struct mlxsw_core *mlxsw_core,
				 u16 local_port)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];
	struct devlink_port *devlink_port = &mlxsw_core_port->devlink_port;

	return devlink_port;
}
EXPORT_SYMBOL(mlxsw_core_port_devlink_port_get);

struct mlxsw_linecard *
mlxsw_core_port_linecard_get(struct mlxsw_core *mlxsw_core,
			     u16 local_port)
{
	struct mlxsw_core_port *mlxsw_core_port =
					&mlxsw_core->ports[local_port];

	return mlxsw_core_port->linecard;
}

void mlxsw_core_ports_remove_selected(struct mlxsw_core *mlxsw_core,
				      bool (*selector)(void *priv, u16 local_port),
				      void *priv)
{
	if (WARN_ON_ONCE(!mlxsw_core->driver->ports_remove_selected))
		return;
	mlxsw_core->driver->ports_remove_selected(mlxsw_core, selector, priv);
}

struct mlxsw_env *mlxsw_core_env(const struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->env;
}

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
		   u32 in_mod, bool out_mbox_direct, bool reset_ok,
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

	if (!err && out_mbox) {
		dev_dbg(mlxsw_core->bus_info->dev, "Output mailbox:\n");
		mlxsw_core_buf_dump_dbg(mlxsw_core, out_mbox, out_mbox_size);
	}

	if (reset_ok && err == -EIO &&
	    status == MLXSW_CMD_STATUS_RUNNING_RESET) {
		err = 0;
	} else if (err == -EIO && status != MLXSW_CMD_STATUS_OK) {
		dev_err(mlxsw_core->bus_info->dev, "Cmd exec failed (opcode=%x(%s),opcode_mod=%x,in_mod=%x,status=%x(%s))\n",
			opcode, mlxsw_cmd_opcode_str(opcode), opcode_mod,
			in_mod, status, mlxsw_cmd_status_str(status));
	} else if (err == -ETIMEDOUT) {
		dev_err(mlxsw_core->bus_info->dev, "Cmd exec timed-out (opcode=%x(%s),opcode_mod=%x,in_mod=%x)\n",
			opcode, mlxsw_cmd_opcode_str(opcode), opcode_mod,
			in_mod);
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

int mlxsw_core_kvd_sizes_get(struct mlxsw_core *mlxsw_core,
			     const struct mlxsw_config_profile *profile,
			     u64 *p_single_size, u64 *p_double_size,
			     u64 *p_linear_size)
{
	struct mlxsw_driver *driver = mlxsw_core->driver;

	if (!driver->kvd_sizes_get)
		return -EINVAL;

	return driver->kvd_sizes_get(mlxsw_core, profile,
				     p_single_size, p_double_size,
				     p_linear_size);
}
EXPORT_SYMBOL(mlxsw_core_kvd_sizes_get);

int mlxsw_core_resources_query(struct mlxsw_core *mlxsw_core, char *mbox,
			       struct mlxsw_res *res)
{
	int index, i;
	u64 data;
	u16 id;
	int err;

	mlxsw_cmd_mbox_zero(mbox);

	for (index = 0; index < MLXSW_CMD_QUERY_RESOURCES_MAX_QUERIES;
	     index++) {
		err = mlxsw_cmd_query_resources(mlxsw_core, mbox, index);
		if (err)
			return err;

		for (i = 0; i < MLXSW_CMD_QUERY_RESOURCES_PER_QUERY; i++) {
			id = mlxsw_cmd_mbox_query_resource_id_get(mbox, i);
			data = mlxsw_cmd_mbox_query_resource_data_get(mbox, i);

			if (id == MLXSW_CMD_QUERY_RESOURCES_TABLE_END_ID)
				return 0;

			mlxsw_res_parse(res, id, data);
		}
	}

	/* If after MLXSW_RESOURCES_QUERY_MAX_QUERIES we still didn't get
	 * MLXSW_RESOURCES_TABLE_END_ID, something went bad in the FW.
	 */
	return -EIO;
}
EXPORT_SYMBOL(mlxsw_core_resources_query);

u32 mlxsw_core_read_frc_h(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->bus->read_frc_h(mlxsw_core->bus_priv);
}
EXPORT_SYMBOL(mlxsw_core_read_frc_h);

u32 mlxsw_core_read_frc_l(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->bus->read_frc_l(mlxsw_core->bus_priv);
}
EXPORT_SYMBOL(mlxsw_core_read_frc_l);

u32 mlxsw_core_read_utc_sec(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->bus->read_utc_sec(mlxsw_core->bus_priv);
}
EXPORT_SYMBOL(mlxsw_core_read_utc_sec);

u32 mlxsw_core_read_utc_nsec(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->bus->read_utc_nsec(mlxsw_core->bus_priv);
}
EXPORT_SYMBOL(mlxsw_core_read_utc_nsec);

bool mlxsw_core_sdq_supports_cqe_v2(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_core->driver->sdq_supports_cqe_v2;
}
EXPORT_SYMBOL(mlxsw_core_sdq_supports_cqe_v2);

void mlxsw_core_emad_string_tlv_enable(struct mlxsw_core *mlxsw_core)
{
	mlxsw_core->emad.enable_string_tlv = true;
}
EXPORT_SYMBOL(mlxsw_core_emad_string_tlv_enable);

static int __init mlxsw_core_module_init(void)
{
	int err;

	err = mlxsw_linecard_driver_register();
	if (err)
		return err;

	mlxsw_wq = alloc_workqueue(mlxsw_core_driver_name, 0, 0);
	if (!mlxsw_wq) {
		err = -ENOMEM;
		goto err_alloc_workqueue;
	}
	mlxsw_owq = alloc_ordered_workqueue("%s_ordered", 0,
					    mlxsw_core_driver_name);
	if (!mlxsw_owq) {
		err = -ENOMEM;
		goto err_alloc_ordered_workqueue;
	}
	return 0;

err_alloc_ordered_workqueue:
	destroy_workqueue(mlxsw_wq);
err_alloc_workqueue:
	mlxsw_linecard_driver_unregister();
	return err;
}

static void __exit mlxsw_core_module_exit(void)
{
	destroy_workqueue(mlxsw_owq);
	destroy_workqueue(mlxsw_wq);
	mlxsw_linecard_driver_unregister();
}

module_init(mlxsw_core_module_init);
module_exit(mlxsw_core_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox switch device core driver");
