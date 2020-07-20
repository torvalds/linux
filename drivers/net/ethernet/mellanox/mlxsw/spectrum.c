// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/dcbnl.h>
#include <linux/inetdevice.h>
#include <linux/netlink.h>
#include <linux/jhash.h>
#include <linux/log2.h>
#include <net/switchdev.h>
#include <net/pkt_cls.h>
#include <net/netevent.h>
#include <net/addrconf.h>

#include "spectrum.h"
#include "pci.h"
#include "core.h"
#include "core_env.h"
#include "reg.h"
#include "port.h"
#include "trap.h"
#include "txheader.h"
#include "spectrum_cnt.h"
#include "spectrum_dpipe.h"
#include "spectrum_acl_flex_actions.h"
#include "spectrum_span.h"
#include "spectrum_ptp.h"
#include "spectrum_trap.h"
#include "../mlxfw/mlxfw.h"

#define MLXSW_SP1_FWREV_MAJOR 13
#define MLXSW_SP1_FWREV_MINOR 2000
#define MLXSW_SP1_FWREV_SUBMINOR 2714
#define MLXSW_SP1_FWREV_CAN_RESET_MINOR 1702

static const struct mlxsw_fw_rev mlxsw_sp1_fw_rev = {
	.major = MLXSW_SP1_FWREV_MAJOR,
	.minor = MLXSW_SP1_FWREV_MINOR,
	.subminor = MLXSW_SP1_FWREV_SUBMINOR,
	.can_reset_minor = MLXSW_SP1_FWREV_CAN_RESET_MINOR,
};

#define MLXSW_SP1_FW_FILENAME \
	"mellanox/mlxsw_spectrum-" __stringify(MLXSW_SP1_FWREV_MAJOR) \
	"." __stringify(MLXSW_SP1_FWREV_MINOR) \
	"." __stringify(MLXSW_SP1_FWREV_SUBMINOR) ".mfa2"

#define MLXSW_SP2_FWREV_MAJOR 29
#define MLXSW_SP2_FWREV_MINOR 2000
#define MLXSW_SP2_FWREV_SUBMINOR 2714

static const struct mlxsw_fw_rev mlxsw_sp2_fw_rev = {
	.major = MLXSW_SP2_FWREV_MAJOR,
	.minor = MLXSW_SP2_FWREV_MINOR,
	.subminor = MLXSW_SP2_FWREV_SUBMINOR,
};

#define MLXSW_SP2_FW_FILENAME \
	"mellanox/mlxsw_spectrum2-" __stringify(MLXSW_SP2_FWREV_MAJOR) \
	"." __stringify(MLXSW_SP2_FWREV_MINOR) \
	"." __stringify(MLXSW_SP2_FWREV_SUBMINOR) ".mfa2"

static const char mlxsw_sp1_driver_name[] = "mlxsw_spectrum";
static const char mlxsw_sp2_driver_name[] = "mlxsw_spectrum2";
static const char mlxsw_sp3_driver_name[] = "mlxsw_spectrum3";
static const char mlxsw_sp_driver_version[] = "1.0";

static const unsigned char mlxsw_sp1_mac_mask[ETH_ALEN] = {
	0xff, 0xff, 0xff, 0xff, 0xfc, 0x00
};
static const unsigned char mlxsw_sp2_mac_mask[ETH_ALEN] = {
	0xff, 0xff, 0xff, 0xff, 0xf0, 0x00
};

/* tx_hdr_version
 * Tx header version.
 * Must be set to 1.
 */
MLXSW_ITEM32(tx, hdr, version, 0x00, 28, 4);

/* tx_hdr_ctl
 * Packet control type.
 * 0 - Ethernet control (e.g. EMADs, LACP)
 * 1 - Ethernet data
 */
MLXSW_ITEM32(tx, hdr, ctl, 0x00, 26, 2);

/* tx_hdr_proto
 * Packet protocol type. Must be set to 1 (Ethernet).
 */
MLXSW_ITEM32(tx, hdr, proto, 0x00, 21, 3);

/* tx_hdr_rx_is_router
 * Packet is sent from the router. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, rx_is_router, 0x00, 19, 1);

/* tx_hdr_fid_valid
 * Indicates if the 'fid' field is valid and should be used for
 * forwarding lookup. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid_valid, 0x00, 16, 1);

/* tx_hdr_swid
 * Switch partition ID. Must be set to 0.
 */
MLXSW_ITEM32(tx, hdr, swid, 0x00, 12, 3);

/* tx_hdr_control_tclass
 * Indicates if the packet should use the control TClass and not one
 * of the data TClasses.
 */
MLXSW_ITEM32(tx, hdr, control_tclass, 0x00, 6, 1);

/* tx_hdr_etclass
 * Egress TClass to be used on the egress device on the egress port.
 */
MLXSW_ITEM32(tx, hdr, etclass, 0x00, 0, 4);

/* tx_hdr_port_mid
 * Destination local port for unicast packets.
 * Destination multicast ID for multicast packets.
 *
 * Control packets are directed to a specific egress port, while data
 * packets are transmitted through the CPU port (0) into the switch partition,
 * where forwarding rules are applied.
 */
MLXSW_ITEM32(tx, hdr, port_mid, 0x04, 16, 16);

/* tx_hdr_fid
 * Forwarding ID used for L2 forwarding lookup. Valid only if 'fid_valid' is
 * set, otherwise calculated based on the packet's VID using VID to FID mapping.
 * Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid, 0x08, 0, 16);

/* tx_hdr_type
 * 0 - Data packets
 * 6 - Control packets
 */
MLXSW_ITEM32(tx, hdr, type, 0x0C, 0, 4);

struct mlxsw_sp_mlxfw_dev {
	struct mlxfw_dev mlxfw_dev;
	struct mlxsw_sp *mlxsw_sp;
};

struct mlxsw_sp_ptp_ops {
	struct mlxsw_sp_ptp_clock *
		(*clock_init)(struct mlxsw_sp *mlxsw_sp, struct device *dev);
	void (*clock_fini)(struct mlxsw_sp_ptp_clock *clock);

	struct mlxsw_sp_ptp_state *(*init)(struct mlxsw_sp *mlxsw_sp);
	void (*fini)(struct mlxsw_sp_ptp_state *ptp_state);

	/* Notify a driver that a packet that might be PTP was received. Driver
	 * is responsible for freeing the passed-in SKB.
	 */
	void (*receive)(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			u8 local_port);

	/* Notify a driver that a timestamped packet was transmitted. Driver
	 * is responsible for freeing the passed-in SKB.
	 */
	void (*transmitted)(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			    u8 local_port);

	int (*hwtstamp_get)(struct mlxsw_sp_port *mlxsw_sp_port,
			    struct hwtstamp_config *config);
	int (*hwtstamp_set)(struct mlxsw_sp_port *mlxsw_sp_port,
			    struct hwtstamp_config *config);
	void (*shaper_work)(struct work_struct *work);
	int (*get_ts_info)(struct mlxsw_sp *mlxsw_sp,
			   struct ethtool_ts_info *info);
	int (*get_stats_count)(void);
	void (*get_stats_strings)(u8 **p);
	void (*get_stats)(struct mlxsw_sp_port *mlxsw_sp_port,
			  u64 *data, int data_index);
};

struct mlxsw_sp_span_ops {
	u32 (*buffsize_get)(int mtu, u32 speed);
};

static int mlxsw_sp_component_query(struct mlxfw_dev *mlxfw_dev,
				    u16 component_index, u32 *p_max_size,
				    u8 *p_align_bits, u16 *p_max_write_size)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcqi_pl[MLXSW_REG_MCQI_LEN];
	int err;

	mlxsw_reg_mcqi_pack(mcqi_pl, component_index);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(mcqi), mcqi_pl);
	if (err)
		return err;
	mlxsw_reg_mcqi_unpack(mcqi_pl, p_max_size, p_align_bits,
			      p_max_write_size);

	*p_align_bits = max_t(u8, *p_align_bits, 2);
	*p_max_write_size = min_t(u16, *p_max_write_size,
				  MLXSW_REG_MCDA_MAX_DATA_LEN);
	return 0;
}

static int mlxsw_sp_fsm_lock(struct mlxfw_dev *mlxfw_dev, u32 *fwhandle)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcc_pl[MLXSW_REG_MCC_LEN];
	u8 control_state;
	int err;

	mlxsw_reg_mcc_pack(mcc_pl, 0, 0, 0, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
	if (err)
		return err;

	mlxsw_reg_mcc_unpack(mcc_pl, fwhandle, NULL, &control_state);
	if (control_state != MLXFW_FSM_STATE_IDLE)
		return -EBUSY;

	mlxsw_reg_mcc_pack(mcc_pl,
			   MLXSW_REG_MCC_INSTRUCTION_LOCK_UPDATE_HANDLE,
			   0, *fwhandle, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_sp_fsm_component_update(struct mlxfw_dev *mlxfw_dev,
					 u32 fwhandle, u16 component_index,
					 u32 component_size)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_UPDATE_COMPONENT,
			   component_index, fwhandle, component_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_sp_fsm_block_download(struct mlxfw_dev *mlxfw_dev,
				       u32 fwhandle, u8 *data, u16 size,
				       u32 offset)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcda_pl[MLXSW_REG_MCDA_LEN];

	mlxsw_reg_mcda_pack(mcda_pl, fwhandle, offset, size, data);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mcda), mcda_pl);
}

static int mlxsw_sp_fsm_component_verify(struct mlxfw_dev *mlxfw_dev,
					 u32 fwhandle, u16 component_index)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_VERIFY_COMPONENT,
			   component_index, fwhandle, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_sp_fsm_activate(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_ACTIVATE, 0,
			   fwhandle, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
}

static int mlxsw_sp_fsm_query_state(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
				    enum mlxfw_fsm_state *fsm_state,
				    enum mlxfw_fsm_state_err *fsm_state_err)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcc_pl[MLXSW_REG_MCC_LEN];
	u8 control_state;
	u8 error_code;
	int err;

	mlxsw_reg_mcc_pack(mcc_pl, 0, 0, fwhandle, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
	if (err)
		return err;

	mlxsw_reg_mcc_unpack(mcc_pl, NULL, &error_code, &control_state);
	*fsm_state = control_state;
	*fsm_state_err = min_t(enum mlxfw_fsm_state_err, error_code,
			       MLXFW_FSM_STATE_ERR_MAX);
	return 0;
}

static void mlxsw_sp_fsm_cancel(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl, MLXSW_REG_MCC_INSTRUCTION_CANCEL, 0,
			   fwhandle, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
}

static void mlxsw_sp_fsm_release(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlxsw_sp_mlxfw_dev *mlxsw_sp_mlxfw_dev =
		container_of(mlxfw_dev, struct mlxsw_sp_mlxfw_dev, mlxfw_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_mlxfw_dev->mlxsw_sp;
	char mcc_pl[MLXSW_REG_MCC_LEN];

	mlxsw_reg_mcc_pack(mcc_pl,
			   MLXSW_REG_MCC_INSTRUCTION_RELEASE_UPDATE_HANDLE, 0,
			   fwhandle, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mcc), mcc_pl);
}

static const struct mlxfw_dev_ops mlxsw_sp_mlxfw_dev_ops = {
	.component_query	= mlxsw_sp_component_query,
	.fsm_lock		= mlxsw_sp_fsm_lock,
	.fsm_component_update	= mlxsw_sp_fsm_component_update,
	.fsm_block_download	= mlxsw_sp_fsm_block_download,
	.fsm_component_verify	= mlxsw_sp_fsm_component_verify,
	.fsm_activate		= mlxsw_sp_fsm_activate,
	.fsm_query_state	= mlxsw_sp_fsm_query_state,
	.fsm_cancel		= mlxsw_sp_fsm_cancel,
	.fsm_release		= mlxsw_sp_fsm_release,
};

static int mlxsw_sp_firmware_flash(struct mlxsw_sp *mlxsw_sp,
				   const struct firmware *firmware,
				   struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_mlxfw_dev mlxsw_sp_mlxfw_dev = {
		.mlxfw_dev = {
			.ops = &mlxsw_sp_mlxfw_dev_ops,
			.psid = mlxsw_sp->bus_info->psid,
			.psid_size = strlen(mlxsw_sp->bus_info->psid),
			.devlink = priv_to_devlink(mlxsw_sp->core),
		},
		.mlxsw_sp = mlxsw_sp
	};
	int err;

	mlxsw_core_fw_flash_start(mlxsw_sp->core);
	err = mlxfw_firmware_flash(&mlxsw_sp_mlxfw_dev.mlxfw_dev,
				   firmware, extack);
	mlxsw_core_fw_flash_end(mlxsw_sp->core);

	return err;
}

static int mlxsw_sp_fw_rev_validate(struct mlxsw_sp *mlxsw_sp)
{
	const struct mlxsw_fw_rev *rev = &mlxsw_sp->bus_info->fw_rev;
	const struct mlxsw_fw_rev *req_rev = mlxsw_sp->req_rev;
	const char *fw_filename = mlxsw_sp->fw_filename;
	union devlink_param_value value;
	const struct firmware *firmware;
	int err;

	/* Don't check if driver does not require it */
	if (!req_rev || !fw_filename)
		return 0;

	/* Don't check if devlink 'fw_load_policy' param is 'flash' */
	err = devlink_param_driverinit_value_get(priv_to_devlink(mlxsw_sp->core),
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

	dev_err(mlxsw_sp->bus_info->dev, "The firmware version %d.%d.%d is incompatible with the driver (required >= %d.%d.%d)\n",
		rev->major, rev->minor, rev->subminor, req_rev->major,
		req_rev->minor, req_rev->subminor);
	dev_info(mlxsw_sp->bus_info->dev, "Flashing firmware using file %s\n",
		 fw_filename);

	err = request_firmware_direct(&firmware, fw_filename,
				      mlxsw_sp->bus_info->dev);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Could not request firmware file %s\n",
			fw_filename);
		return err;
	}

	err = mlxsw_sp_firmware_flash(mlxsw_sp, firmware, NULL);
	release_firmware(firmware);
	if (err)
		dev_err(mlxsw_sp->bus_info->dev, "Could not upgrade firmware\n");

	/* On FW flash success, tell the caller FW reset is needed
	 * if current FW supports it.
	 */
	if (rev->minor >= req_rev->can_reset_minor)
		return err ? err : -EAGAIN;
	else
		return 0;
}

static int mlxsw_sp_flash_update(struct mlxsw_core *mlxsw_core,
				 const char *file_name, const char *component,
				 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	const struct firmware *firmware;
	int err;

	if (component)
		return -EOPNOTSUPP;

	err = request_firmware_direct(&firmware, file_name,
				      mlxsw_sp->bus_info->dev);
	if (err)
		return err;
	err = mlxsw_sp_firmware_flash(mlxsw_sp, firmware, extack);
	release_firmware(firmware);

	return err;
}

int mlxsw_sp_flow_counter_get(struct mlxsw_sp *mlxsw_sp,
			      unsigned int counter_index, u64 *packets,
			      u64 *bytes)
{
	char mgpc_pl[MLXSW_REG_MGPC_LEN];
	int err;

	mlxsw_reg_mgpc_pack(mgpc_pl, counter_index, MLXSW_REG_MGPC_OPCODE_NOP,
			    MLXSW_REG_FLOW_COUNTER_SET_TYPE_PACKETS_BYTES);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(mgpc), mgpc_pl);
	if (err)
		return err;
	if (packets)
		*packets = mlxsw_reg_mgpc_packet_counter_get(mgpc_pl);
	if (bytes)
		*bytes = mlxsw_reg_mgpc_byte_counter_get(mgpc_pl);
	return 0;
}

static int mlxsw_sp_flow_counter_clear(struct mlxsw_sp *mlxsw_sp,
				       unsigned int counter_index)
{
	char mgpc_pl[MLXSW_REG_MGPC_LEN];

	mlxsw_reg_mgpc_pack(mgpc_pl, counter_index, MLXSW_REG_MGPC_OPCODE_CLEAR,
			    MLXSW_REG_FLOW_COUNTER_SET_TYPE_PACKETS_BYTES);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mgpc), mgpc_pl);
}

int mlxsw_sp_flow_counter_alloc(struct mlxsw_sp *mlxsw_sp,
				unsigned int *p_counter_index)
{
	int err;

	err = mlxsw_sp_counter_alloc(mlxsw_sp, MLXSW_SP_COUNTER_SUB_POOL_FLOW,
				     p_counter_index);
	if (err)
		return err;
	err = mlxsw_sp_flow_counter_clear(mlxsw_sp, *p_counter_index);
	if (err)
		goto err_counter_clear;
	return 0;

err_counter_clear:
	mlxsw_sp_counter_free(mlxsw_sp, MLXSW_SP_COUNTER_SUB_POOL_FLOW,
			      *p_counter_index);
	return err;
}

void mlxsw_sp_flow_counter_free(struct mlxsw_sp *mlxsw_sp,
				unsigned int counter_index)
{
	 mlxsw_sp_counter_free(mlxsw_sp, MLXSW_SP_COUNTER_SUB_POOL_FLOW,
			       counter_index);
}

static void mlxsw_sp_txhdr_construct(struct sk_buff *skb,
				     const struct mlxsw_tx_info *tx_info)
{
	char *txhdr = skb_push(skb, MLXSW_TXHDR_LEN);

	memset(txhdr, 0, MLXSW_TXHDR_LEN);

	mlxsw_tx_hdr_version_set(txhdr, MLXSW_TXHDR_VERSION_1);
	mlxsw_tx_hdr_ctl_set(txhdr, MLXSW_TXHDR_ETH_CTL);
	mlxsw_tx_hdr_proto_set(txhdr, MLXSW_TXHDR_PROTO_ETH);
	mlxsw_tx_hdr_swid_set(txhdr, 0);
	mlxsw_tx_hdr_control_tclass_set(txhdr, 1);
	mlxsw_tx_hdr_port_mid_set(txhdr, tx_info->local_port);
	mlxsw_tx_hdr_type_set(txhdr, MLXSW_TXHDR_TYPE_CONTROL);
}

enum mlxsw_reg_spms_state mlxsw_sp_stp_spms_state(u8 state)
{
	switch (state) {
	case BR_STATE_FORWARDING:
		return MLXSW_REG_SPMS_STATE_FORWARDING;
	case BR_STATE_LEARNING:
		return MLXSW_REG_SPMS_STATE_LEARNING;
	case BR_STATE_LISTENING: /* fall-through */
	case BR_STATE_DISABLED: /* fall-through */
	case BR_STATE_BLOCKING:
		return MLXSW_REG_SPMS_STATE_DISCARDING;
	default:
		BUG();
	}
}

int mlxsw_sp_port_vid_stp_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid,
			      u8 state)
{
	enum mlxsw_reg_spms_state spms_state = mlxsw_sp_stp_spms_state(state);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spms_pl;
	int err;

	spms_pl = kmalloc(MLXSW_REG_SPMS_LEN, GFP_KERNEL);
	if (!spms_pl)
		return -ENOMEM;
	mlxsw_reg_spms_pack(spms_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_spms_vid_pack(spms_pl, vid, spms_state);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spms), spms_pl);
	kfree(spms_pl);
	return err;
}

static int mlxsw_sp_base_mac_get(struct mlxsw_sp *mlxsw_sp)
{
	char spad_pl[MLXSW_REG_SPAD_LEN] = {0};
	int err;

	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(spad), spad_pl);
	if (err)
		return err;
	mlxsw_reg_spad_base_mac_memcpy_from(spad_pl, mlxsw_sp->base_mac);
	return 0;
}

static int mlxsw_sp_port_admin_status_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  bool is_up)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char paos_pl[MLXSW_REG_PAOS_LEN];

	mlxsw_reg_paos_pack(paos_pl, mlxsw_sp_port->local_port,
			    is_up ? MLXSW_PORT_ADMIN_STATUS_UP :
			    MLXSW_PORT_ADMIN_STATUS_DOWN);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(paos), paos_pl);
}

static int mlxsw_sp_port_dev_addr_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      unsigned char *addr)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ppad_pl[MLXSW_REG_PPAD_LEN];

	mlxsw_reg_ppad_pack(ppad_pl, true, mlxsw_sp_port->local_port);
	mlxsw_reg_ppad_mac_memcpy_to(ppad_pl, addr);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppad), ppad_pl);
}

static int mlxsw_sp_port_dev_addr_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	unsigned char *addr = mlxsw_sp_port->dev->dev_addr;

	ether_addr_copy(addr, mlxsw_sp->base_mac);
	addr[ETH_ALEN - 1] += mlxsw_sp_port->local_port;
	return mlxsw_sp_port_dev_addr_set(mlxsw_sp_port, addr);
}

static int mlxsw_sp_port_mtu_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 mtu)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pmtu_pl[MLXSW_REG_PMTU_LEN];
	int max_mtu;
	int err;

	mtu += MLXSW_TXHDR_LEN + ETH_HLEN;
	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pmtu), pmtu_pl);
	if (err)
		return err;
	max_mtu = mlxsw_reg_pmtu_max_mtu_get(pmtu_pl);

	if (mtu > max_mtu)
		return -EINVAL;

	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sp_port->local_port, mtu);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmtu), pmtu_pl);
}

static int mlxsw_sp_port_swid_set(struct mlxsw_sp_port *mlxsw_sp_port, u8 swid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pspa_pl[MLXSW_REG_PSPA_LEN];

	mlxsw_reg_pspa_pack(pspa_pl, swid, mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pspa), pspa_pl);
}

int mlxsw_sp_port_vp_mode_set(struct mlxsw_sp_port *mlxsw_sp_port, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char svpe_pl[MLXSW_REG_SVPE_LEN];

	mlxsw_reg_svpe_pack(svpe_pl, mlxsw_sp_port->local_port, enable);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svpe), svpe_pl);
}

int mlxsw_sp_port_vid_learning_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid,
				   bool learn_enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spvmlr_pl;
	int err;

	spvmlr_pl = kmalloc(MLXSW_REG_SPVMLR_LEN, GFP_KERNEL);
	if (!spvmlr_pl)
		return -ENOMEM;
	mlxsw_reg_spvmlr_pack(spvmlr_pl, mlxsw_sp_port->local_port, vid, vid,
			      learn_enable);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvmlr), spvmlr_pl);
	kfree(spvmlr_pl);
	return err;
}

static int __mlxsw_sp_port_pvid_set(struct mlxsw_sp_port *mlxsw_sp_port,
				    u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char spvid_pl[MLXSW_REG_SPVID_LEN];

	mlxsw_reg_spvid_pack(spvid_pl, mlxsw_sp_port->local_port, vid);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvid), spvid_pl);
}

static int mlxsw_sp_port_allow_untagged_set(struct mlxsw_sp_port *mlxsw_sp_port,
					    bool allow)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char spaft_pl[MLXSW_REG_SPAFT_LEN];

	mlxsw_reg_spaft_pack(spaft_pl, mlxsw_sp_port->local_port, allow);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spaft), spaft_pl);
}

int mlxsw_sp_port_pvid_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	int err;

	if (!vid) {
		err = mlxsw_sp_port_allow_untagged_set(mlxsw_sp_port, false);
		if (err)
			return err;
	} else {
		err = __mlxsw_sp_port_pvid_set(mlxsw_sp_port, vid);
		if (err)
			return err;
		err = mlxsw_sp_port_allow_untagged_set(mlxsw_sp_port, true);
		if (err)
			goto err_port_allow_untagged_set;
	}

	mlxsw_sp_port->pvid = vid;
	return 0;

err_port_allow_untagged_set:
	__mlxsw_sp_port_pvid_set(mlxsw_sp_port, mlxsw_sp_port->pvid);
	return err;
}

static int
mlxsw_sp_port_system_port_mapping_set(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sspr_pl[MLXSW_REG_SSPR_LEN];

	mlxsw_reg_sspr_pack(sspr_pl, mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sspr), sspr_pl);
}

static int
mlxsw_sp_port_module_info_get(struct mlxsw_sp *mlxsw_sp, u8 local_port,
			      struct mlxsw_sp_port_mapping *port_mapping)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	bool separate_rxtx;
	u8 module;
	u8 width;
	int err;
	int i;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
	if (err)
		return err;
	module = mlxsw_reg_pmlp_module_get(pmlp_pl, 0);
	width = mlxsw_reg_pmlp_width_get(pmlp_pl);
	separate_rxtx = mlxsw_reg_pmlp_rxtx_get(pmlp_pl);

	if (width && !is_power_of_2(width)) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Unsupported module config: width value is not power of 2\n",
			local_port);
		return -EINVAL;
	}

	for (i = 0; i < width; i++) {
		if (mlxsw_reg_pmlp_module_get(pmlp_pl, i) != module) {
			dev_err(mlxsw_sp->bus_info->dev, "Port %d: Unsupported module config: contains multiple modules\n",
				local_port);
			return -EINVAL;
		}
		if (separate_rxtx &&
		    mlxsw_reg_pmlp_tx_lane_get(pmlp_pl, i) !=
		    mlxsw_reg_pmlp_rx_lane_get(pmlp_pl, i)) {
			dev_err(mlxsw_sp->bus_info->dev, "Port %d: Unsupported module config: TX and RX lane numbers are different\n",
				local_port);
			return -EINVAL;
		}
		if (mlxsw_reg_pmlp_tx_lane_get(pmlp_pl, i) != i) {
			dev_err(mlxsw_sp->bus_info->dev, "Port %d: Unsupported module config: TX and RX lane numbers are not sequential\n",
				local_port);
			return -EINVAL;
		}
	}

	port_mapping->module = module;
	port_mapping->width = width;
	port_mapping->lane = mlxsw_reg_pmlp_tx_lane_get(pmlp_pl, 0);
	return 0;
}

static int mlxsw_sp_port_module_map(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_port_mapping *port_mapping = &mlxsw_sp_port->mapping;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int i;

	mlxsw_reg_pmlp_pack(pmlp_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_pmlp_width_set(pmlp_pl, port_mapping->width);
	for (i = 0; i < port_mapping->width; i++) {
		mlxsw_reg_pmlp_module_set(pmlp_pl, i, port_mapping->module);
		mlxsw_reg_pmlp_tx_lane_set(pmlp_pl, i, port_mapping->lane + i); /* Rx & Tx */
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
}

static int mlxsw_sp_port_module_unmap(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pmlp_pl[MLXSW_REG_PMLP_LEN];

	mlxsw_reg_pmlp_pack(pmlp_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_pmlp_width_set(pmlp_pl, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
}

static int mlxsw_sp_port_open(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);
	if (err)
		return err;
	netif_start_queue(dev);
	return 0;
}

static int mlxsw_sp_port_stop(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	netif_stop_queue(dev);
	return mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
}

static netdev_tx_t mlxsw_sp_port_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_pcpu_stats *pcpu_stats;
	const struct mlxsw_tx_info tx_info = {
		.local_port = mlxsw_sp_port->local_port,
		.is_emad = false,
	};
	u64 len;
	int err;

	if (skb_cow_head(skb, MLXSW_TXHDR_LEN)) {
		this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	memset(skb->cb, 0, sizeof(struct mlxsw_skb_cb));

	if (mlxsw_core_skb_transmit_busy(mlxsw_sp->core, &tx_info))
		return NETDEV_TX_BUSY;

	if (eth_skb_pad(skb)) {
		this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
		return NETDEV_TX_OK;
	}

	mlxsw_sp_txhdr_construct(skb, &tx_info);
	/* TX header is consumed by HW on the way so we shouldn't count its
	 * bytes as being sent.
	 */
	len = skb->len - MLXSW_TXHDR_LEN;

	/* Due to a race we might fail here because of a full queue. In that
	 * unlikely case we simply drop the packet.
	 */
	err = mlxsw_core_skb_transmit(mlxsw_sp->core, skb, &tx_info);

	if (!err) {
		pcpu_stats = this_cpu_ptr(mlxsw_sp_port->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->tx_packets++;
		pcpu_stats->tx_bytes += len;
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
		dev_kfree_skb_any(skb);
	}
	return NETDEV_TX_OK;
}

static void mlxsw_sp_set_rx_mode(struct net_device *dev)
{
}

static int mlxsw_sp_port_set_mac_address(struct net_device *dev, void *p)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = mlxsw_sp_port_dev_addr_set(mlxsw_sp_port, addr->sa_data);
	if (err)
		return err;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}

static u16 mlxsw_sp_pg_buf_threshold_get(const struct mlxsw_sp *mlxsw_sp,
					 int mtu)
{
	return 2 * mlxsw_sp_bytes_cells(mlxsw_sp, mtu);
}

#define MLXSW_SP_CELL_FACTOR 2	/* 2 * cell_size / (IPG + cell_size + 1) */

static u16 mlxsw_sp_pfc_delay_get(const struct mlxsw_sp *mlxsw_sp, int mtu,
				  u16 delay)
{
	delay = mlxsw_sp_bytes_cells(mlxsw_sp, DIV_ROUND_UP(delay,
							    BITS_PER_BYTE));
	return MLXSW_SP_CELL_FACTOR * delay + mlxsw_sp_bytes_cells(mlxsw_sp,
								   mtu);
}

/* Maximum delay buffer needed in case of PAUSE frames, in bytes.
 * Assumes 100m cable and maximum MTU.
 */
#define MLXSW_SP_PAUSE_DELAY 58752

static u16 mlxsw_sp_pg_buf_delay_get(const struct mlxsw_sp *mlxsw_sp, int mtu,
				     u16 delay, bool pfc, bool pause)
{
	if (pfc)
		return mlxsw_sp_pfc_delay_get(mlxsw_sp, mtu, delay);
	else if (pause)
		return mlxsw_sp_bytes_cells(mlxsw_sp, MLXSW_SP_PAUSE_DELAY);
	else
		return 0;
}

static void mlxsw_sp_pg_buf_pack(char *pbmc_pl, int index, u16 size, u16 thres,
				 bool lossy)
{
	if (lossy)
		mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl, index, size);
	else
		mlxsw_reg_pbmc_lossless_buffer_pack(pbmc_pl, index, size,
						    thres);
}

int __mlxsw_sp_port_headroom_set(struct mlxsw_sp_port *mlxsw_sp_port, int mtu,
				 u8 *prio_tc, bool pause_en,
				 struct ieee_pfc *my_pfc)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 pfc_en = !!my_pfc ? my_pfc->pfc_en : 0;
	u16 delay = !!my_pfc ? my_pfc->delay : 0;
	char pbmc_pl[MLXSW_REG_PBMC_LEN];
	u32 taken_headroom_cells = 0;
	u32 max_headroom_cells;
	int i, j, err;

	max_headroom_cells = mlxsw_sp_sb_max_headroom_cells(mlxsw_sp);

	mlxsw_reg_pbmc_pack(pbmc_pl, mlxsw_sp_port->local_port, 0, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pbmc), pbmc_pl);
	if (err)
		return err;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		bool configure = false;
		bool pfc = false;
		u16 thres_cells;
		u16 delay_cells;
		u16 total_cells;
		bool lossy;

		for (j = 0; j < IEEE_8021QAZ_MAX_TCS; j++) {
			if (prio_tc[j] == i) {
				pfc = pfc_en & BIT(j);
				configure = true;
				break;
			}
		}

		if (!configure)
			continue;

		lossy = !(pfc || pause_en);
		thres_cells = mlxsw_sp_pg_buf_threshold_get(mlxsw_sp, mtu);
		thres_cells = mlxsw_sp_port_headroom_8x_adjust(mlxsw_sp_port, thres_cells);
		delay_cells = mlxsw_sp_pg_buf_delay_get(mlxsw_sp, mtu, delay,
							pfc, pause_en);
		delay_cells = mlxsw_sp_port_headroom_8x_adjust(mlxsw_sp_port, delay_cells);
		total_cells = thres_cells + delay_cells;

		taken_headroom_cells += total_cells;
		if (taken_headroom_cells > max_headroom_cells)
			return -ENOBUFS;

		mlxsw_sp_pg_buf_pack(pbmc_pl, i, total_cells,
				     thres_cells, lossy);
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pbmc), pbmc_pl);
}

static int mlxsw_sp_port_headroom_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      int mtu, bool pause_en)
{
	u8 def_prio_tc[IEEE_8021QAZ_MAX_TCS] = {0};
	bool dcb_en = !!mlxsw_sp_port->dcb.ets;
	struct ieee_pfc *my_pfc;
	u8 *prio_tc;

	prio_tc = dcb_en ? mlxsw_sp_port->dcb.ets->prio_tc : def_prio_tc;
	my_pfc = dcb_en ? mlxsw_sp_port->dcb.pfc : NULL;

	return __mlxsw_sp_port_headroom_set(mlxsw_sp_port, mtu, prio_tc,
					    pause_en, my_pfc);
}

static int mlxsw_sp_port_change_mtu(struct net_device *dev, int mtu)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	bool pause_en = mlxsw_sp_port_is_pause_en(mlxsw_sp_port);
	int err;

	err = mlxsw_sp_port_headroom_set(mlxsw_sp_port, mtu, pause_en);
	if (err)
		return err;
	err = mlxsw_sp_span_port_mtu_update(mlxsw_sp_port, mtu);
	if (err)
		goto err_span_port_mtu_update;
	err = mlxsw_sp_port_mtu_set(mlxsw_sp_port, mtu);
	if (err)
		goto err_port_mtu_set;
	dev->mtu = mtu;
	return 0;

err_port_mtu_set:
	mlxsw_sp_span_port_mtu_update(mlxsw_sp_port, dev->mtu);
err_span_port_mtu_update:
	mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu, pause_en);
	return err;
}

static int
mlxsw_sp_port_get_sw_stats64(const struct net_device *dev,
			     struct rtnl_link_stats64 *stats)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp_port_pcpu_stats *p;
	u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
	u32 tx_dropped = 0;
	unsigned int start;
	int i;

	for_each_possible_cpu(i) {
		p = per_cpu_ptr(mlxsw_sp_port->pcpu_stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			rx_packets	= p->rx_packets;
			rx_bytes	= p->rx_bytes;
			tx_packets	= p->tx_packets;
			tx_bytes	= p->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));

		stats->rx_packets	+= rx_packets;
		stats->rx_bytes		+= rx_bytes;
		stats->tx_packets	+= tx_packets;
		stats->tx_bytes		+= tx_bytes;
		/* tx_dropped is u32, updated without syncp protection. */
		tx_dropped	+= p->tx_dropped;
	}
	stats->tx_dropped	= tx_dropped;
	return 0;
}

static bool mlxsw_sp_port_has_offload_stats(const struct net_device *dev, int attr_id)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return true;
	}

	return false;
}

static int mlxsw_sp_port_get_offload_stats(int attr_id, const struct net_device *dev,
					   void *sp)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return mlxsw_sp_port_get_sw_stats64(dev, sp);
	}

	return -EINVAL;
}

static int mlxsw_sp_port_get_stats_raw(struct net_device *dev, int grp,
				       int prio, char *ppcnt_pl)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	mlxsw_reg_ppcnt_pack(ppcnt_pl, mlxsw_sp_port->local_port, grp, prio);
	return mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ppcnt), ppcnt_pl);
}

static int mlxsw_sp_port_get_hw_stats(struct net_device *dev,
				      struct rtnl_link_stats64 *stats)
{
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int err;

	err = mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT,
					  0, ppcnt_pl);
	if (err)
		goto out;

	stats->tx_packets =
		mlxsw_reg_ppcnt_a_frames_transmitted_ok_get(ppcnt_pl);
	stats->rx_packets =
		mlxsw_reg_ppcnt_a_frames_received_ok_get(ppcnt_pl);
	stats->tx_bytes =
		mlxsw_reg_ppcnt_a_octets_transmitted_ok_get(ppcnt_pl);
	stats->rx_bytes =
		mlxsw_reg_ppcnt_a_octets_received_ok_get(ppcnt_pl);
	stats->multicast =
		mlxsw_reg_ppcnt_a_multicast_frames_received_ok_get(ppcnt_pl);

	stats->rx_crc_errors =
		mlxsw_reg_ppcnt_a_frame_check_sequence_errors_get(ppcnt_pl);
	stats->rx_frame_errors =
		mlxsw_reg_ppcnt_a_alignment_errors_get(ppcnt_pl);

	stats->rx_length_errors = (
		mlxsw_reg_ppcnt_a_in_range_length_errors_get(ppcnt_pl) +
		mlxsw_reg_ppcnt_a_out_of_range_length_field_get(ppcnt_pl) +
		mlxsw_reg_ppcnt_a_frame_too_long_errors_get(ppcnt_pl));

	stats->rx_errors = (stats->rx_crc_errors +
		stats->rx_frame_errors + stats->rx_length_errors);

out:
	return err;
}

static void
mlxsw_sp_port_get_hw_xstats(struct net_device *dev,
			    struct mlxsw_sp_port_xstats *xstats)
{
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int err, i;

	err = mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_EXT_CNT, 0,
					  ppcnt_pl);
	if (!err)
		xstats->ecn = mlxsw_reg_ppcnt_ecn_marked_get(ppcnt_pl);

	for (i = 0; i < TC_MAX_QUEUE; i++) {
		err = mlxsw_sp_port_get_stats_raw(dev,
						  MLXSW_REG_PPCNT_TC_CONG_TC,
						  i, ppcnt_pl);
		if (!err)
			xstats->wred_drop[i] =
				mlxsw_reg_ppcnt_wred_discard_get(ppcnt_pl);

		err = mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_TC_CNT,
						  i, ppcnt_pl);
		if (err)
			continue;

		xstats->backlog[i] =
			mlxsw_reg_ppcnt_tc_transmit_queue_get(ppcnt_pl);
		xstats->tail_drop[i] =
			mlxsw_reg_ppcnt_tc_no_buffer_discard_uc_get(ppcnt_pl);
	}

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_PRIO_CNT,
						  i, ppcnt_pl);
		if (err)
			continue;

		xstats->tx_packets[i] = mlxsw_reg_ppcnt_tx_frames_get(ppcnt_pl);
		xstats->tx_bytes[i] = mlxsw_reg_ppcnt_tx_octets_get(ppcnt_pl);
	}
}

static void update_stats_cache(struct work_struct *work)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
		container_of(work, struct mlxsw_sp_port,
			     periodic_hw_stats.update_dw.work);

	if (!netif_carrier_ok(mlxsw_sp_port->dev))
		/* Note: mlxsw_sp_port_down_wipe_counters() clears the cache as
		 * necessary when port goes down.
		 */
		goto out;

	mlxsw_sp_port_get_hw_stats(mlxsw_sp_port->dev,
				   &mlxsw_sp_port->periodic_hw_stats.stats);
	mlxsw_sp_port_get_hw_xstats(mlxsw_sp_port->dev,
				    &mlxsw_sp_port->periodic_hw_stats.xstats);

out:
	mlxsw_core_schedule_dw(&mlxsw_sp_port->periodic_hw_stats.update_dw,
			       MLXSW_HW_STATS_UPDATE_TIME);
}

/* Return the stats from a cache that is updated periodically,
 * as this function might get called in an atomic context.
 */
static void
mlxsw_sp_port_get_stats64(struct net_device *dev,
			  struct rtnl_link_stats64 *stats)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	memcpy(stats, &mlxsw_sp_port->periodic_hw_stats.stats, sizeof(*stats));
}

static int __mlxsw_sp_port_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port,
				    u16 vid_begin, u16 vid_end,
				    bool is_member, bool untagged)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spvm_pl;
	int err;

	spvm_pl = kmalloc(MLXSW_REG_SPVM_LEN, GFP_KERNEL);
	if (!spvm_pl)
		return -ENOMEM;

	mlxsw_reg_spvm_pack(spvm_pl, mlxsw_sp_port->local_port,	vid_begin,
			    vid_end, is_member, untagged);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvm), spvm_pl);
	kfree(spvm_pl);
	return err;
}

int mlxsw_sp_port_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid_begin,
			   u16 vid_end, bool is_member, bool untagged)
{
	u16 vid, vid_e;
	int err;

	for (vid = vid_begin; vid <= vid_end;
	     vid += MLXSW_REG_SPVM_REC_MAX_COUNT) {
		vid_e = min((u16) (vid + MLXSW_REG_SPVM_REC_MAX_COUNT - 1),
			    vid_end);

		err = __mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid_e,
					       is_member, untagged);
		if (err)
			return err;
	}

	return 0;
}

static void mlxsw_sp_port_vlan_flush(struct mlxsw_sp_port *mlxsw_sp_port,
				     bool flush_default)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan, *tmp;

	list_for_each_entry_safe(mlxsw_sp_port_vlan, tmp,
				 &mlxsw_sp_port->vlans_list, list) {
		if (!flush_default &&
		    mlxsw_sp_port_vlan->vid == MLXSW_SP_DEFAULT_VID)
			continue;
		mlxsw_sp_port_vlan_destroy(mlxsw_sp_port_vlan);
	}
}

static void
mlxsw_sp_port_vlan_cleanup(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	if (mlxsw_sp_port_vlan->bridge_port)
		mlxsw_sp_port_vlan_bridge_leave(mlxsw_sp_port_vlan);
	else if (mlxsw_sp_port_vlan->fid)
		mlxsw_sp_port_vlan_router_leave(mlxsw_sp_port_vlan);
}

struct mlxsw_sp_port_vlan *
mlxsw_sp_port_vlan_create(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	bool untagged = vid == MLXSW_SP_DEFAULT_VID;
	int err;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (mlxsw_sp_port_vlan)
		return ERR_PTR(-EEXIST);

	err = mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, true, untagged);
	if (err)
		return ERR_PTR(err);

	mlxsw_sp_port_vlan = kzalloc(sizeof(*mlxsw_sp_port_vlan), GFP_KERNEL);
	if (!mlxsw_sp_port_vlan) {
		err = -ENOMEM;
		goto err_port_vlan_alloc;
	}

	mlxsw_sp_port_vlan->mlxsw_sp_port = mlxsw_sp_port;
	mlxsw_sp_port_vlan->vid = vid;
	list_add(&mlxsw_sp_port_vlan->list, &mlxsw_sp_port->vlans_list);

	return mlxsw_sp_port_vlan;

err_port_vlan_alloc:
	mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, false, false);
	return ERR_PTR(err);
}

void mlxsw_sp_port_vlan_destroy(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	u16 vid = mlxsw_sp_port_vlan->vid;

	mlxsw_sp_port_vlan_cleanup(mlxsw_sp_port_vlan);
	list_del(&mlxsw_sp_port_vlan->list);
	kfree(mlxsw_sp_port_vlan);
	mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, false, false);
}

static int mlxsw_sp_port_add_vid(struct net_device *dev,
				 __be16 __always_unused proto, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	/* VLAN 0 is added to HW filter when device goes up, but it is
	 * reserved in our case, so simply return.
	 */
	if (!vid)
		return 0;

	return PTR_ERR_OR_ZERO(mlxsw_sp_port_vlan_create(mlxsw_sp_port, vid));
}

static int mlxsw_sp_port_kill_vid(struct net_device *dev,
				  __be16 __always_unused proto, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	/* VLAN 0 is removed from HW filter when device goes down, but
	 * it is reserved in our case, so simply return.
	 */
	if (!vid)
		return 0;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (!mlxsw_sp_port_vlan)
		return 0;
	mlxsw_sp_port_vlan_destroy(mlxsw_sp_port_vlan);

	return 0;
}

static int mlxsw_sp_setup_tc(struct net_device *dev, enum tc_setup_type type,
			     void *type_data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return mlxsw_sp_setup_tc_block(mlxsw_sp_port, type_data);
	case TC_SETUP_QDISC_RED:
		return mlxsw_sp_setup_tc_red(mlxsw_sp_port, type_data);
	case TC_SETUP_QDISC_PRIO:
		return mlxsw_sp_setup_tc_prio(mlxsw_sp_port, type_data);
	case TC_SETUP_QDISC_ETS:
		return mlxsw_sp_setup_tc_ets(mlxsw_sp_port, type_data);
	case TC_SETUP_QDISC_TBF:
		return mlxsw_sp_setup_tc_tbf(mlxsw_sp_port, type_data);
	case TC_SETUP_QDISC_FIFO:
		return mlxsw_sp_setup_tc_fifo(mlxsw_sp_port, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlxsw_sp_feature_hw_tc(struct net_device *dev, bool enable)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	if (!enable) {
		if (mlxsw_sp_flow_block_rule_count(mlxsw_sp_port->ing_flow_block) ||
		    mlxsw_sp_flow_block_rule_count(mlxsw_sp_port->eg_flow_block)) {
			netdev_err(dev, "Active offloaded tc filters, can't turn hw_tc_offload off\n");
			return -EINVAL;
		}
		mlxsw_sp_flow_block_disable_inc(mlxsw_sp_port->ing_flow_block);
		mlxsw_sp_flow_block_disable_inc(mlxsw_sp_port->eg_flow_block);
	} else {
		mlxsw_sp_flow_block_disable_dec(mlxsw_sp_port->ing_flow_block);
		mlxsw_sp_flow_block_disable_dec(mlxsw_sp_port->eg_flow_block);
	}
	return 0;
}

static int mlxsw_sp_feature_loopback(struct net_device *dev, bool enable)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	char pplr_pl[MLXSW_REG_PPLR_LEN];
	int err;

	if (netif_running(dev))
		mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);

	mlxsw_reg_pplr_pack(pplr_pl, mlxsw_sp_port->local_port, enable);
	err = mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pplr),
			      pplr_pl);

	if (netif_running(dev))
		mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);

	return err;
}

typedef int (*mlxsw_sp_feature_handler)(struct net_device *dev, bool enable);

static int mlxsw_sp_handle_feature(struct net_device *dev,
				   netdev_features_t wanted_features,
				   netdev_features_t feature,
				   mlxsw_sp_feature_handler feature_handler)
{
	netdev_features_t changes = wanted_features ^ dev->features;
	bool enable = !!(wanted_features & feature);
	int err;

	if (!(changes & feature))
		return 0;

	err = feature_handler(dev, enable);
	if (err) {
		netdev_err(dev, "%s feature %pNF failed, err %d\n",
			   enable ? "Enable" : "Disable", &feature, err);
		return err;
	}

	if (enable)
		dev->features |= feature;
	else
		dev->features &= ~feature;

	return 0;
}
static int mlxsw_sp_set_features(struct net_device *dev,
				 netdev_features_t features)
{
	netdev_features_t oper_features = dev->features;
	int err = 0;

	err |= mlxsw_sp_handle_feature(dev, features, NETIF_F_HW_TC,
				       mlxsw_sp_feature_hw_tc);
	err |= mlxsw_sp_handle_feature(dev, features, NETIF_F_LOOPBACK,
				       mlxsw_sp_feature_loopback);

	if (err) {
		dev->features = oper_features;
		return -EINVAL;
	}

	return 0;
}

static struct devlink_port *
mlxsw_sp_port_get_devlink_port(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	return mlxsw_core_port_devlink_port_get(mlxsw_sp->core,
						mlxsw_sp_port->local_port);
}

static int mlxsw_sp_port_hwtstamp_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = mlxsw_sp_port->mlxsw_sp->ptp_ops->hwtstamp_set(mlxsw_sp_port,
							     &config);
	if (err)
		return err;

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}

static int mlxsw_sp_port_hwtstamp_get(struct mlxsw_sp_port *mlxsw_sp_port,
				      struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	err = mlxsw_sp_port->mlxsw_sp->ptp_ops->hwtstamp_get(mlxsw_sp_port,
							     &config);
	if (err)
		return err;

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}

static inline void mlxsw_sp_port_ptp_clear(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct hwtstamp_config config = {0};

	mlxsw_sp_port->mlxsw_sp->ptp_ops->hwtstamp_set(mlxsw_sp_port, &config);
}

static int
mlxsw_sp_port_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return mlxsw_sp_port_hwtstamp_set(mlxsw_sp_port, ifr);
	case SIOCGHWTSTAMP:
		return mlxsw_sp_port_hwtstamp_get(mlxsw_sp_port, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops mlxsw_sp_port_netdev_ops = {
	.ndo_open		= mlxsw_sp_port_open,
	.ndo_stop		= mlxsw_sp_port_stop,
	.ndo_start_xmit		= mlxsw_sp_port_xmit,
	.ndo_setup_tc           = mlxsw_sp_setup_tc,
	.ndo_set_rx_mode	= mlxsw_sp_set_rx_mode,
	.ndo_set_mac_address	= mlxsw_sp_port_set_mac_address,
	.ndo_change_mtu		= mlxsw_sp_port_change_mtu,
	.ndo_get_stats64	= mlxsw_sp_port_get_stats64,
	.ndo_has_offload_stats	= mlxsw_sp_port_has_offload_stats,
	.ndo_get_offload_stats	= mlxsw_sp_port_get_offload_stats,
	.ndo_vlan_rx_add_vid	= mlxsw_sp_port_add_vid,
	.ndo_vlan_rx_kill_vid	= mlxsw_sp_port_kill_vid,
	.ndo_set_features	= mlxsw_sp_set_features,
	.ndo_get_devlink_port	= mlxsw_sp_port_get_devlink_port,
	.ndo_do_ioctl		= mlxsw_sp_port_ioctl,
};

static void mlxsw_sp_port_get_drvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *drvinfo)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	strlcpy(drvinfo->driver, mlxsw_sp->bus_info->device_kind,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, mlxsw_sp_driver_version,
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 mlxsw_sp->bus_info->fw_rev.major,
		 mlxsw_sp->bus_info->fw_rev.minor,
		 mlxsw_sp->bus_info->fw_rev.subminor);
	strlcpy(drvinfo->bus_info, mlxsw_sp->bus_info->device_name,
		sizeof(drvinfo->bus_info));
}

static void mlxsw_sp_port_get_pauseparam(struct net_device *dev,
					 struct ethtool_pauseparam *pause)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	pause->rx_pause = mlxsw_sp_port->link.rx_pause;
	pause->tx_pause = mlxsw_sp_port->link.tx_pause;
}

static int mlxsw_sp_port_pause_set(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct ethtool_pauseparam *pause)
{
	char pfcc_pl[MLXSW_REG_PFCC_LEN];

	mlxsw_reg_pfcc_pack(pfcc_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_pfcc_pprx_set(pfcc_pl, pause->rx_pause);
	mlxsw_reg_pfcc_pptx_set(pfcc_pl, pause->tx_pause);

	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pfcc),
			       pfcc_pl);
}

static int mlxsw_sp_port_set_pauseparam(struct net_device *dev,
					struct ethtool_pauseparam *pause)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	bool pause_en = pause->tx_pause || pause->rx_pause;
	int err;

	if (mlxsw_sp_port->dcb.pfc && mlxsw_sp_port->dcb.pfc->pfc_en) {
		netdev_err(dev, "PFC already enabled on port\n");
		return -EINVAL;
	}

	if (pause->autoneg) {
		netdev_err(dev, "PAUSE frames autonegotiation isn't supported\n");
		return -EINVAL;
	}

	err = mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu, pause_en);
	if (err) {
		netdev_err(dev, "Failed to configure port's headroom\n");
		return err;
	}

	err = mlxsw_sp_port_pause_set(mlxsw_sp_port, pause);
	if (err) {
		netdev_err(dev, "Failed to set PAUSE parameters\n");
		goto err_port_pause_configure;
	}

	mlxsw_sp_port->link.rx_pause = pause->rx_pause;
	mlxsw_sp_port->link.tx_pause = pause->tx_pause;

	return 0;

err_port_pause_configure:
	pause_en = mlxsw_sp_port_is_pause_en(mlxsw_sp_port);
	mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu, pause_en);
	return err;
}

struct mlxsw_sp_port_hw_stats {
	char str[ETH_GSTRING_LEN];
	u64 (*getter)(const char *payload);
	bool cells_bytes;
};

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_stats[] = {
	{
		.str = "a_frames_transmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_frames_transmitted_ok_get,
	},
	{
		.str = "a_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_frames_received_ok_get,
	},
	{
		.str = "a_frame_check_sequence_errors",
		.getter = mlxsw_reg_ppcnt_a_frame_check_sequence_errors_get,
	},
	{
		.str = "a_alignment_errors",
		.getter = mlxsw_reg_ppcnt_a_alignment_errors_get,
	},
	{
		.str = "a_octets_transmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_octets_transmitted_ok_get,
	},
	{
		.str = "a_octets_received_ok",
		.getter = mlxsw_reg_ppcnt_a_octets_received_ok_get,
	},
	{
		.str = "a_multicast_frames_xmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_multicast_frames_xmitted_ok_get,
	},
	{
		.str = "a_broadcast_frames_xmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_broadcast_frames_xmitted_ok_get,
	},
	{
		.str = "a_multicast_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_multicast_frames_received_ok_get,
	},
	{
		.str = "a_broadcast_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_broadcast_frames_received_ok_get,
	},
	{
		.str = "a_in_range_length_errors",
		.getter = mlxsw_reg_ppcnt_a_in_range_length_errors_get,
	},
	{
		.str = "a_out_of_range_length_field",
		.getter = mlxsw_reg_ppcnt_a_out_of_range_length_field_get,
	},
	{
		.str = "a_frame_too_long_errors",
		.getter = mlxsw_reg_ppcnt_a_frame_too_long_errors_get,
	},
	{
		.str = "a_symbol_error_during_carrier",
		.getter = mlxsw_reg_ppcnt_a_symbol_error_during_carrier_get,
	},
	{
		.str = "a_mac_control_frames_transmitted",
		.getter = mlxsw_reg_ppcnt_a_mac_control_frames_transmitted_get,
	},
	{
		.str = "a_mac_control_frames_received",
		.getter = mlxsw_reg_ppcnt_a_mac_control_frames_received_get,
	},
	{
		.str = "a_unsupported_opcodes_received",
		.getter = mlxsw_reg_ppcnt_a_unsupported_opcodes_received_get,
	},
	{
		.str = "a_pause_mac_ctrl_frames_received",
		.getter = mlxsw_reg_ppcnt_a_pause_mac_ctrl_frames_received_get,
	},
	{
		.str = "a_pause_mac_ctrl_frames_xmitted",
		.getter = mlxsw_reg_ppcnt_a_pause_mac_ctrl_frames_transmitted_get,
	},
};

#define MLXSW_SP_PORT_HW_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_rfc_2863_stats[] = {
	{
		.str = "if_in_discards",
		.getter = mlxsw_reg_ppcnt_if_in_discards_get,
	},
	{
		.str = "if_out_discards",
		.getter = mlxsw_reg_ppcnt_if_out_discards_get,
	},
	{
		.str = "if_out_errors",
		.getter = mlxsw_reg_ppcnt_if_out_errors_get,
	},
};

#define MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_rfc_2863_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_rfc_2819_stats[] = {
	{
		.str = "ether_stats_undersize_pkts",
		.getter = mlxsw_reg_ppcnt_ether_stats_undersize_pkts_get,
	},
	{
		.str = "ether_stats_oversize_pkts",
		.getter = mlxsw_reg_ppcnt_ether_stats_oversize_pkts_get,
	},
	{
		.str = "ether_stats_fragments",
		.getter = mlxsw_reg_ppcnt_ether_stats_fragments_get,
	},
	{
		.str = "ether_pkts64octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts64octets_get,
	},
	{
		.str = "ether_pkts65to127octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts65to127octets_get,
	},
	{
		.str = "ether_pkts128to255octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts128to255octets_get,
	},
	{
		.str = "ether_pkts256to511octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts256to511octets_get,
	},
	{
		.str = "ether_pkts512to1023octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts512to1023octets_get,
	},
	{
		.str = "ether_pkts1024to1518octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts1024to1518octets_get,
	},
	{
		.str = "ether_pkts1519to2047octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts1519to2047octets_get,
	},
	{
		.str = "ether_pkts2048to4095octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts2048to4095octets_get,
	},
	{
		.str = "ether_pkts4096to8191octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts4096to8191octets_get,
	},
	{
		.str = "ether_pkts8192to10239octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts8192to10239octets_get,
	},
};

#define MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_rfc_2819_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_rfc_3635_stats[] = {
	{
		.str = "dot3stats_fcs_errors",
		.getter = mlxsw_reg_ppcnt_dot3stats_fcs_errors_get,
	},
	{
		.str = "dot3stats_symbol_errors",
		.getter = mlxsw_reg_ppcnt_dot3stats_symbol_errors_get,
	},
	{
		.str = "dot3control_in_unknown_opcodes",
		.getter = mlxsw_reg_ppcnt_dot3control_in_unknown_opcodes_get,
	},
	{
		.str = "dot3in_pause_frames",
		.getter = mlxsw_reg_ppcnt_dot3in_pause_frames_get,
	},
};

#define MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_rfc_3635_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_ext_stats[] = {
	{
		.str = "ecn_marked",
		.getter = mlxsw_reg_ppcnt_ecn_marked_get,
	},
};

#define MLXSW_SP_PORT_HW_EXT_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_ext_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_discard_stats[] = {
	{
		.str = "discard_ingress_general",
		.getter = mlxsw_reg_ppcnt_ingress_general_get,
	},
	{
		.str = "discard_ingress_policy_engine",
		.getter = mlxsw_reg_ppcnt_ingress_policy_engine_get,
	},
	{
		.str = "discard_ingress_vlan_membership",
		.getter = mlxsw_reg_ppcnt_ingress_vlan_membership_get,
	},
	{
		.str = "discard_ingress_tag_frame_type",
		.getter = mlxsw_reg_ppcnt_ingress_tag_frame_type_get,
	},
	{
		.str = "discard_egress_vlan_membership",
		.getter = mlxsw_reg_ppcnt_egress_vlan_membership_get,
	},
	{
		.str = "discard_loopback_filter",
		.getter = mlxsw_reg_ppcnt_loopback_filter_get,
	},
	{
		.str = "discard_egress_general",
		.getter = mlxsw_reg_ppcnt_egress_general_get,
	},
	{
		.str = "discard_egress_hoq",
		.getter = mlxsw_reg_ppcnt_egress_hoq_get,
	},
	{
		.str = "discard_egress_policy_engine",
		.getter = mlxsw_reg_ppcnt_egress_policy_engine_get,
	},
	{
		.str = "discard_ingress_tx_link_down",
		.getter = mlxsw_reg_ppcnt_ingress_tx_link_down_get,
	},
	{
		.str = "discard_egress_stp_filter",
		.getter = mlxsw_reg_ppcnt_egress_stp_filter_get,
	},
	{
		.str = "discard_egress_sll",
		.getter = mlxsw_reg_ppcnt_egress_sll_get,
	},
};

#define MLXSW_SP_PORT_HW_DISCARD_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_discard_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_prio_stats[] = {
	{
		.str = "rx_octets_prio",
		.getter = mlxsw_reg_ppcnt_rx_octets_get,
	},
	{
		.str = "rx_frames_prio",
		.getter = mlxsw_reg_ppcnt_rx_frames_get,
	},
	{
		.str = "tx_octets_prio",
		.getter = mlxsw_reg_ppcnt_tx_octets_get,
	},
	{
		.str = "tx_frames_prio",
		.getter = mlxsw_reg_ppcnt_tx_frames_get,
	},
	{
		.str = "rx_pause_prio",
		.getter = mlxsw_reg_ppcnt_rx_pause_get,
	},
	{
		.str = "rx_pause_duration_prio",
		.getter = mlxsw_reg_ppcnt_rx_pause_duration_get,
	},
	{
		.str = "tx_pause_prio",
		.getter = mlxsw_reg_ppcnt_tx_pause_get,
	},
	{
		.str = "tx_pause_duration_prio",
		.getter = mlxsw_reg_ppcnt_tx_pause_duration_get,
	},
};

#define MLXSW_SP_PORT_HW_PRIO_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_prio_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_tc_stats[] = {
	{
		.str = "tc_transmit_queue_tc",
		.getter = mlxsw_reg_ppcnt_tc_transmit_queue_get,
		.cells_bytes = true,
	},
	{
		.str = "tc_no_buffer_discard_uc_tc",
		.getter = mlxsw_reg_ppcnt_tc_no_buffer_discard_uc_get,
	},
};

#define MLXSW_SP_PORT_HW_TC_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_tc_stats)

#define MLXSW_SP_PORT_ETHTOOL_STATS_LEN (MLXSW_SP_PORT_HW_STATS_LEN + \
					 MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN + \
					 MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN + \
					 MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN + \
					 MLXSW_SP_PORT_HW_EXT_STATS_LEN + \
					 MLXSW_SP_PORT_HW_DISCARD_STATS_LEN + \
					 (MLXSW_SP_PORT_HW_PRIO_STATS_LEN * \
					  IEEE_8021QAZ_MAX_TCS) + \
					 (MLXSW_SP_PORT_HW_TC_STATS_LEN * \
					  TC_MAX_QUEUE))

static void mlxsw_sp_port_get_prio_strings(u8 **p, int prio)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_HW_PRIO_STATS_LEN; i++) {
		snprintf(*p, ETH_GSTRING_LEN, "%.29s_%.1d",
			 mlxsw_sp_port_hw_prio_stats[i].str, prio);
		*p += ETH_GSTRING_LEN;
	}
}

static void mlxsw_sp_port_get_tc_strings(u8 **p, int tc)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_HW_TC_STATS_LEN; i++) {
		snprintf(*p, ETH_GSTRING_LEN, "%.29s_%.1d",
			 mlxsw_sp_port_hw_tc_stats[i].str, tc);
		*p += ETH_GSTRING_LEN;
	}
}

static void mlxsw_sp_port_get_strings(struct net_device *dev,
				      u32 stringset, u8 *data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < MLXSW_SP_PORT_HW_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_rfc_2863_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_rfc_2819_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_rfc_3635_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_EXT_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_ext_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_DISCARD_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_discard_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
			mlxsw_sp_port_get_prio_strings(&p, i);

		for (i = 0; i < TC_MAX_QUEUE; i++)
			mlxsw_sp_port_get_tc_strings(&p, i);

		mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats_strings(&p);
		break;
	}
}

static int mlxsw_sp_port_set_phys_id(struct net_device *dev,
				     enum ethtool_phys_id_state state)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char mlcr_pl[MLXSW_REG_MLCR_LEN];
	bool active;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		active = true;
		break;
	case ETHTOOL_ID_INACTIVE:
		active = false;
		break;
	default:
		return -EOPNOTSUPP;
	}

	mlxsw_reg_mlcr_pack(mlcr_pl, mlxsw_sp_port->local_port, active);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mlcr), mlcr_pl);
}

static int
mlxsw_sp_get_hw_stats_by_group(struct mlxsw_sp_port_hw_stats **p_hw_stats,
			       int *p_len, enum mlxsw_reg_ppcnt_grp grp)
{
	switch (grp) {
	case MLXSW_REG_PPCNT_IEEE_8023_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_stats;
		*p_len = MLXSW_SP_PORT_HW_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_RFC_2863_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_rfc_2863_stats;
		*p_len = MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_RFC_2819_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_rfc_2819_stats;
		*p_len = MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_RFC_3635_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_rfc_3635_stats;
		*p_len = MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_EXT_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_ext_stats;
		*p_len = MLXSW_SP_PORT_HW_EXT_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_DISCARD_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_discard_stats;
		*p_len = MLXSW_SP_PORT_HW_DISCARD_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_PRIO_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_prio_stats;
		*p_len = MLXSW_SP_PORT_HW_PRIO_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_TC_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_tc_stats;
		*p_len = MLXSW_SP_PORT_HW_TC_STATS_LEN;
		break;
	default:
		WARN_ON(1);
		return -EOPNOTSUPP;
	}
	return 0;
}

static void __mlxsw_sp_port_get_stats(struct net_device *dev,
				      enum mlxsw_reg_ppcnt_grp grp, int prio,
				      u64 *data, int data_index)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_hw_stats *hw_stats;
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int i, len;
	int err;

	err = mlxsw_sp_get_hw_stats_by_group(&hw_stats, &len, grp);
	if (err)
		return;
	mlxsw_sp_port_get_stats_raw(dev, grp, prio, ppcnt_pl);
	for (i = 0; i < len; i++) {
		data[data_index + i] = hw_stats[i].getter(ppcnt_pl);
		if (!hw_stats[i].cells_bytes)
			continue;
		data[data_index + i] = mlxsw_sp_cells_bytes(mlxsw_sp,
							    data[data_index + i]);
	}
}

static void mlxsw_sp_port_get_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int i, data_index = 0;

	/* IEEE 802.3 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT, 0,
				  data, data_index);
	data_index = MLXSW_SP_PORT_HW_STATS_LEN;

	/* RFC 2863 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_RFC_2863_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN;

	/* RFC 2819 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_RFC_2819_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN;

	/* RFC 3635 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_RFC_3635_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN;

	/* Extended Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_EXT_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_EXT_STATS_LEN;

	/* Discard Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_DISCARD_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_DISCARD_STATS_LEN;

	/* Per-Priority Counters */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_PRIO_CNT, i,
					  data, data_index);
		data_index += MLXSW_SP_PORT_HW_PRIO_STATS_LEN;
	}

	/* Per-TC Counters */
	for (i = 0; i < TC_MAX_QUEUE; i++) {
		__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_TC_CNT, i,
					  data, data_index);
		data_index += MLXSW_SP_PORT_HW_TC_STATS_LEN;
	}

	/* PTP counters */
	mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats(mlxsw_sp_port,
						    data, data_index);
	data_index += mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats_count();
}

static int mlxsw_sp_port_get_sset_count(struct net_device *dev, int sset)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return MLXSW_SP_PORT_ETHTOOL_STATS_LEN +
		       mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats_count();
	default:
		return -EOPNOTSUPP;
	}
}

struct mlxsw_sp1_port_link_mode {
	enum ethtool_link_mode_bit_indices mask_ethtool;
	u32 mask;
	u32 speed;
};

static const struct mlxsw_sp1_port_link_mode mlxsw_sp1_port_link_mode[] = {
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100BASE_T,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100baseT_Full_BIT,
		.speed		= SPEED_100,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_SGMII |
				  MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX,
		.mask_ethtool	= ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
		.speed		= SPEED_1000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_T,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CX4 |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_ER_LR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_20GBASE_KR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT,
		.speed		= SPEED_20000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_LR4_ER4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_CR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_KR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_SR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_CR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_KR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_SR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_LR4_ER4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
		.speed		= SPEED_100000,
	},
};

#define MLXSW_SP1_PORT_LINK_MODE_LEN ARRAY_SIZE(mlxsw_sp1_port_link_mode)

static void
mlxsw_sp1_from_ptys_supported_port(struct mlxsw_sp *mlxsw_sp,
				   u32 ptys_eth_proto,
				   struct ethtool_link_ksettings *cmd)
{
	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_SGMII))
		ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX))
		ethtool_link_ksettings_add_link_mode(cmd, supported, Backplane);
}

static void
mlxsw_sp1_from_ptys_link(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto,
			 u8 width, unsigned long *mode)
{
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp1_port_link_mode[i].mask)
			__set_bit(mlxsw_sp1_port_link_mode[i].mask_ethtool,
				  mode);
	}
}

static u32
mlxsw_sp1_from_ptys_speed(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto)
{
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp1_port_link_mode[i].mask)
			return mlxsw_sp1_port_link_mode[i].speed;
	}

	return SPEED_UNKNOWN;
}

static void
mlxsw_sp1_from_ptys_speed_duplex(struct mlxsw_sp *mlxsw_sp, bool carrier_ok,
				 u32 ptys_eth_proto,
				 struct ethtool_link_ksettings *cmd)
{
	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;

	if (!carrier_ok)
		return;

	cmd->base.speed = mlxsw_sp1_from_ptys_speed(mlxsw_sp, ptys_eth_proto);
	if (cmd->base.speed != SPEED_UNKNOWN)
		cmd->base.duplex = DUPLEX_FULL;
}

static u32
mlxsw_sp1_to_ptys_advert_link(struct mlxsw_sp *mlxsw_sp, u8 width,
			      const struct ethtool_link_ksettings *cmd)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (test_bit(mlxsw_sp1_port_link_mode[i].mask_ethtool,
			     cmd->link_modes.advertising))
			ptys_proto |= mlxsw_sp1_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp1_to_ptys_speed(struct mlxsw_sp *mlxsw_sp, u8 width,
				   u32 speed)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (speed == mlxsw_sp1_port_link_mode[i].speed)
			ptys_proto |= mlxsw_sp1_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static void
mlxsw_sp1_reg_ptys_eth_pack(struct mlxsw_sp *mlxsw_sp, char *payload,
			    u8 local_port, u32 proto_admin, bool autoneg)
{
	mlxsw_reg_ptys_eth_pack(payload, local_port, proto_admin, autoneg);
}

static void
mlxsw_sp1_reg_ptys_eth_unpack(struct mlxsw_sp *mlxsw_sp, char *payload,
			      u32 *p_eth_proto_cap, u32 *p_eth_proto_admin,
			      u32 *p_eth_proto_oper)
{
	mlxsw_reg_ptys_eth_unpack(payload, p_eth_proto_cap, p_eth_proto_admin,
				  p_eth_proto_oper);
}

static const struct mlxsw_sp_port_type_speed_ops
mlxsw_sp1_port_type_speed_ops = {
	.from_ptys_supported_port	= mlxsw_sp1_from_ptys_supported_port,
	.from_ptys_link			= mlxsw_sp1_from_ptys_link,
	.from_ptys_speed		= mlxsw_sp1_from_ptys_speed,
	.from_ptys_speed_duplex		= mlxsw_sp1_from_ptys_speed_duplex,
	.to_ptys_advert_link		= mlxsw_sp1_to_ptys_advert_link,
	.to_ptys_speed			= mlxsw_sp1_to_ptys_speed,
	.reg_ptys_eth_pack		= mlxsw_sp1_reg_ptys_eth_pack,
	.reg_ptys_eth_unpack		= mlxsw_sp1_reg_ptys_eth_unpack,
};

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_sgmii_100m[] = {
	ETHTOOL_LINK_MODE_100baseT_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_SGMII_100M_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_sgmii_100m)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_1000base_x_sgmii[] = {
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_1000BASE_X_SGMII_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_1000base_x_sgmii)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_2_5gbase_x_2_5gmii[] = {
	ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_2_5GBASE_X_2_5GMII_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_2_5gbase_x_2_5gmii)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_5gbase_r[] = {
	ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_5GBASE_R_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_5gbase_r)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_xfi_xaui_1_10g[] = {
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
	ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseER_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_XFI_XAUI_1_10G_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_xfi_xaui_1_10g)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_xlaui_4_xlppi_4_40g[] = {
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_XLAUI_4_XLPPI_4_40G_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_xlaui_4_xlppi_4_40g)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_25gaui_1_25gbase_cr_kr[] = {
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_25GAUI_1_25GBASE_CR_KR_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_25gaui_1_25gbase_cr_kr)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_50gaui_2_laui_2_50gbase_cr2_kr2[] = {
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_50GAUI_2_LAUI_2_50GBASE_CR2_KR2_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_50gaui_2_laui_2_50gbase_cr2_kr2)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_50gaui_1_laui_1_50gbase_cr_kr[] = {
	ETHTOOL_LINK_MODE_50000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseDR_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_50GAUI_1_LAUI_1_50GBASE_CR_KR_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_50gaui_1_laui_1_50gbase_cr_kr)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_caui_4_100gbase_cr4_kr4[] = {
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_CAUI_4_100GBASE_CR4_KR4_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_caui_4_100gbase_cr4_kr4)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_100gaui_2_100gbase_cr2_kr2[] = {
	ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_100GAUI_2_100GBASE_CR2_KR2_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_100gaui_2_100gbase_cr2_kr2)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_200gaui_4_200gbase_cr4_kr4[] = {
	ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseLR4_ER4_FR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseDR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_200GAUI_4_200GBASE_CR4_KR4_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_200gaui_4_200gbase_cr4_kr4)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_400gaui_8[] = {
	ETHTOOL_LINK_MODE_400000baseKR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseSR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseLR8_ER8_FR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseDR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseCR8_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_400GAUI_8_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_400gaui_8)

#define MLXSW_SP_PORT_MASK_WIDTH_1X	BIT(0)
#define MLXSW_SP_PORT_MASK_WIDTH_2X	BIT(1)
#define MLXSW_SP_PORT_MASK_WIDTH_4X	BIT(2)
#define MLXSW_SP_PORT_MASK_WIDTH_8X	BIT(3)

static u8 mlxsw_sp_port_mask_width_get(u8 width)
{
	switch (width) {
	case 1:
		return MLXSW_SP_PORT_MASK_WIDTH_1X;
	case 2:
		return MLXSW_SP_PORT_MASK_WIDTH_2X;
	case 4:
		return MLXSW_SP_PORT_MASK_WIDTH_4X;
	case 8:
		return MLXSW_SP_PORT_MASK_WIDTH_8X;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

struct mlxsw_sp2_port_link_mode {
	const enum ethtool_link_mode_bit_indices *mask_ethtool;
	int m_ethtool_len;
	u32 mask;
	u32 speed;
	u8 mask_width;
};

static const struct mlxsw_sp2_port_link_mode mlxsw_sp2_port_link_mode[] = {
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_SGMII_100M,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_sgmii_100m,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_SGMII_100M_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_100,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_1000BASE_X_SGMII,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_1000base_x_sgmii,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_1000BASE_X_SGMII_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_1000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_2_5GBASE_X_2_5GMII,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_2_5gbase_x_2_5gmii,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_2_5GBASE_X_2_5GMII_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_2500,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_5GBASE_R,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_5gbase_r,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_5GBASE_R_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_5000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_XFI_XAUI_1_10G,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_xfi_xaui_1_10g,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_XFI_XAUI_1_10G_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_XLAUI_4_XLPPI_4_40G,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_xlaui_4_xlppi_4_40g,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_XLAUI_4_XLPPI_4_40G_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_25GAUI_1_25GBASE_CR_KR,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_25gaui_1_25gbase_cr_kr,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_25GAUI_1_25GBASE_CR_KR_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_50GAUI_2_LAUI_2_50GBASE_CR2_KR2,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_50gaui_2_laui_2_50gbase_cr2_kr2,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_50GAUI_2_LAUI_2_50GBASE_CR2_KR2_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_50GAUI_1_LAUI_1_50GBASE_CR_KR,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_50gaui_1_laui_1_50gbase_cr_kr,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_50GAUI_1_LAUI_1_50GBASE_CR_KR_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_1X,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_CAUI_4_100GBASE_CR4_KR4,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_caui_4_100gbase_cr4_kr4,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_CAUI_4_100GBASE_CR4_KR4_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_100GAUI_2_100GBASE_CR2_KR2,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_100gaui_2_100gbase_cr2_kr2,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_100GAUI_2_100GBASE_CR2_KR2_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_2X,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_200GAUI_4_200GBASE_CR4_KR4,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_200gaui_4_200gbase_cr4_kr4,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_200GAUI_4_200GBASE_CR4_KR4_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_200000,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_400GAUI_8,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_400gaui_8,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_400GAUI_8_LEN,
		.mask_width	= MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_400000,
	},
};

#define MLXSW_SP2_PORT_LINK_MODE_LEN ARRAY_SIZE(mlxsw_sp2_port_link_mode)

static void
mlxsw_sp2_from_ptys_supported_port(struct mlxsw_sp *mlxsw_sp,
				   u32 ptys_eth_proto,
				   struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Backplane);
}

static void
mlxsw_sp2_set_bit_ethtool(const struct mlxsw_sp2_port_link_mode *link_mode,
			  unsigned long *mode)
{
	int i;

	for (i = 0; i < link_mode->m_ethtool_len; i++)
		__set_bit(link_mode->mask_ethtool[i], mode);
}

static void
mlxsw_sp2_from_ptys_link(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto,
			 u8 width, unsigned long *mode)
{
	u8 mask_width = mlxsw_sp_port_mask_width_get(width);
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if ((ptys_eth_proto & mlxsw_sp2_port_link_mode[i].mask) &&
		    (mask_width & mlxsw_sp2_port_link_mode[i].mask_width))
			mlxsw_sp2_set_bit_ethtool(&mlxsw_sp2_port_link_mode[i],
						  mode);
	}
}

static u32
mlxsw_sp2_from_ptys_speed(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto)
{
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp2_port_link_mode[i].mask)
			return mlxsw_sp2_port_link_mode[i].speed;
	}

	return SPEED_UNKNOWN;
}

static void
mlxsw_sp2_from_ptys_speed_duplex(struct mlxsw_sp *mlxsw_sp, bool carrier_ok,
				 u32 ptys_eth_proto,
				 struct ethtool_link_ksettings *cmd)
{
	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;

	if (!carrier_ok)
		return;

	cmd->base.speed = mlxsw_sp2_from_ptys_speed(mlxsw_sp, ptys_eth_proto);
	if (cmd->base.speed != SPEED_UNKNOWN)
		cmd->base.duplex = DUPLEX_FULL;
}

static bool
mlxsw_sp2_test_bit_ethtool(const struct mlxsw_sp2_port_link_mode *link_mode,
			   const unsigned long *mode)
{
	int cnt = 0;
	int i;

	for (i = 0; i < link_mode->m_ethtool_len; i++) {
		if (test_bit(link_mode->mask_ethtool[i], mode))
			cnt++;
	}

	return cnt == link_mode->m_ethtool_len;
}

static u32
mlxsw_sp2_to_ptys_advert_link(struct mlxsw_sp *mlxsw_sp, u8 width,
			      const struct ethtool_link_ksettings *cmd)
{
	u8 mask_width = mlxsw_sp_port_mask_width_get(width);
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if ((mask_width & mlxsw_sp2_port_link_mode[i].mask_width) &&
		    mlxsw_sp2_test_bit_ethtool(&mlxsw_sp2_port_link_mode[i],
					       cmd->link_modes.advertising))
			ptys_proto |= mlxsw_sp2_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp2_to_ptys_speed(struct mlxsw_sp *mlxsw_sp,
				   u8 width, u32 speed)
{
	u8 mask_width = mlxsw_sp_port_mask_width_get(width);
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if ((speed == mlxsw_sp2_port_link_mode[i].speed) &&
		    (mask_width & mlxsw_sp2_port_link_mode[i].mask_width))
			ptys_proto |= mlxsw_sp2_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static void
mlxsw_sp2_reg_ptys_eth_pack(struct mlxsw_sp *mlxsw_sp, char *payload,
			    u8 local_port, u32 proto_admin,
			    bool autoneg)
{
	mlxsw_reg_ptys_ext_eth_pack(payload, local_port, proto_admin, autoneg);
}

static void
mlxsw_sp2_reg_ptys_eth_unpack(struct mlxsw_sp *mlxsw_sp, char *payload,
			      u32 *p_eth_proto_cap, u32 *p_eth_proto_admin,
			      u32 *p_eth_proto_oper)
{
	mlxsw_reg_ptys_ext_eth_unpack(payload, p_eth_proto_cap,
				      p_eth_proto_admin, p_eth_proto_oper);
}

static const struct mlxsw_sp_port_type_speed_ops
mlxsw_sp2_port_type_speed_ops = {
	.from_ptys_supported_port	= mlxsw_sp2_from_ptys_supported_port,
	.from_ptys_link			= mlxsw_sp2_from_ptys_link,
	.from_ptys_speed		= mlxsw_sp2_from_ptys_speed,
	.from_ptys_speed_duplex		= mlxsw_sp2_from_ptys_speed_duplex,
	.to_ptys_advert_link		= mlxsw_sp2_to_ptys_advert_link,
	.to_ptys_speed			= mlxsw_sp2_to_ptys_speed,
	.reg_ptys_eth_pack		= mlxsw_sp2_reg_ptys_eth_pack,
	.reg_ptys_eth_unpack		= mlxsw_sp2_reg_ptys_eth_unpack,
};

static void
mlxsw_sp_port_get_link_supported(struct mlxsw_sp *mlxsw_sp, u32 eth_proto_cap,
				 u8 width, struct ethtool_link_ksettings *cmd)
{
	const struct mlxsw_sp_port_type_speed_ops *ops;

	ops = mlxsw_sp->port_type_speed_ops;

	ethtool_link_ksettings_add_link_mode(cmd, supported, Asym_Pause);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);

	ops->from_ptys_supported_port(mlxsw_sp, eth_proto_cap, cmd);
	ops->from_ptys_link(mlxsw_sp, eth_proto_cap, width,
			    cmd->link_modes.supported);
}

static void
mlxsw_sp_port_get_link_advertise(struct mlxsw_sp *mlxsw_sp,
				 u32 eth_proto_admin, bool autoneg, u8 width,
				 struct ethtool_link_ksettings *cmd)
{
	const struct mlxsw_sp_port_type_speed_ops *ops;

	ops = mlxsw_sp->port_type_speed_ops;

	if (!autoneg)
		return;

	ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);
	ops->from_ptys_link(mlxsw_sp, eth_proto_admin, width,
			    cmd->link_modes.advertising);
}

static u8
mlxsw_sp_port_connector_port(enum mlxsw_reg_ptys_connector_type connector_type)
{
	switch (connector_type) {
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_UNKNOWN_OR_NO_CONNECTOR:
		return PORT_OTHER;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_NONE:
		return PORT_NONE;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_TP:
		return PORT_TP;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_AUI:
		return PORT_AUI;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_BNC:
		return PORT_BNC;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_MII:
		return PORT_MII;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_FIBRE:
		return PORT_FIBRE;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_DA:
		return PORT_DA;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_OTHER:
		return PORT_OTHER;
	default:
		WARN_ON_ONCE(1);
		return PORT_OTHER;
	}
}

static int mlxsw_sp_port_get_link_ksettings(struct net_device *dev,
					    struct ethtool_link_ksettings *cmd)
{
	u32 eth_proto_cap, eth_proto_admin, eth_proto_oper;
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	const struct mlxsw_sp_port_type_speed_ops *ops;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u8 connector_type;
	bool autoneg;
	int err;

	ops = mlxsw_sp->port_type_speed_ops;

	autoneg = mlxsw_sp_port->link.autoneg;
	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port,
			       0, false);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;
	ops->reg_ptys_eth_unpack(mlxsw_sp, ptys_pl, &eth_proto_cap,
				 &eth_proto_admin, &eth_proto_oper);

	mlxsw_sp_port_get_link_supported(mlxsw_sp, eth_proto_cap,
					 mlxsw_sp_port->mapping.width, cmd);

	mlxsw_sp_port_get_link_advertise(mlxsw_sp, eth_proto_admin, autoneg,
					 mlxsw_sp_port->mapping.width, cmd);

	cmd->base.autoneg = autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	connector_type = mlxsw_reg_ptys_connector_type_get(ptys_pl);
	cmd->base.port = mlxsw_sp_port_connector_port(connector_type);
	ops->from_ptys_speed_duplex(mlxsw_sp, netif_carrier_ok(dev),
				    eth_proto_oper, cmd);

	return 0;
}

static int
mlxsw_sp_port_set_link_ksettings(struct net_device *dev,
				 const struct ethtool_link_ksettings *cmd)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	const struct mlxsw_sp_port_type_speed_ops *ops;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_cap, eth_proto_new;
	bool autoneg;
	int err;

	ops = mlxsw_sp->port_type_speed_ops;

	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port,
			       0, false);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;
	ops->reg_ptys_eth_unpack(mlxsw_sp, ptys_pl, &eth_proto_cap, NULL, NULL);

	autoneg = cmd->base.autoneg == AUTONEG_ENABLE;
	eth_proto_new = autoneg ?
		ops->to_ptys_advert_link(mlxsw_sp, mlxsw_sp_port->mapping.width,
					 cmd) :
		ops->to_ptys_speed(mlxsw_sp, mlxsw_sp_port->mapping.width,
				   cmd->base.speed);

	eth_proto_new = eth_proto_new & eth_proto_cap;
	if (!eth_proto_new) {
		netdev_err(dev, "No supported speed requested\n");
		return -EINVAL;
	}

	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port,
			       eth_proto_new, autoneg);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;

	mlxsw_sp_port->link.autoneg = autoneg;

	if (!netif_running(dev))
		return 0;

	mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
	mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);

	return 0;
}

static int mlxsw_sp_get_module_info(struct net_device *netdev,
				    struct ethtool_modinfo *modinfo)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(netdev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	int err;

	err = mlxsw_env_get_module_info(mlxsw_sp->core,
					mlxsw_sp_port->mapping.module,
					modinfo);

	return err;
}

static int mlxsw_sp_get_module_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee,
				      u8 *data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(netdev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	int err;

	err = mlxsw_env_get_module_eeprom(netdev, mlxsw_sp->core,
					  mlxsw_sp_port->mapping.module, ee,
					  data);

	return err;
}

static int
mlxsw_sp_get_ts_info(struct net_device *netdev, struct ethtool_ts_info *info)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(netdev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	return mlxsw_sp->ptp_ops->get_ts_info(mlxsw_sp, info);
}

static const struct ethtool_ops mlxsw_sp_port_ethtool_ops = {
	.get_drvinfo		= mlxsw_sp_port_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_pauseparam		= mlxsw_sp_port_get_pauseparam,
	.set_pauseparam		= mlxsw_sp_port_set_pauseparam,
	.get_strings		= mlxsw_sp_port_get_strings,
	.set_phys_id		= mlxsw_sp_port_set_phys_id,
	.get_ethtool_stats	= mlxsw_sp_port_get_stats,
	.get_sset_count		= mlxsw_sp_port_get_sset_count,
	.get_link_ksettings	= mlxsw_sp_port_get_link_ksettings,
	.set_link_ksettings	= mlxsw_sp_port_set_link_ksettings,
	.get_module_info	= mlxsw_sp_get_module_info,
	.get_module_eeprom	= mlxsw_sp_get_module_eeprom,
	.get_ts_info		= mlxsw_sp_get_ts_info,
};

static int
mlxsw_sp_port_speed_by_width_set(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u32 eth_proto_cap, eth_proto_admin, eth_proto_oper;
	const struct mlxsw_sp_port_type_speed_ops *ops;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	int err;

	ops = mlxsw_sp->port_type_speed_ops;

	/* Set advertised speeds to supported speeds. */
	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port,
			       0, false);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;

	ops->reg_ptys_eth_unpack(mlxsw_sp, ptys_pl, &eth_proto_cap,
				 &eth_proto_admin, &eth_proto_oper);
	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port,
			       eth_proto_cap, mlxsw_sp_port->link.autoneg);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
}

int mlxsw_sp_port_speed_get(struct mlxsw_sp_port *mlxsw_sp_port, u32 *speed)
{
	const struct mlxsw_sp_port_type_speed_ops *port_type_speed_ops;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_oper;
	int err;

	port_type_speed_ops = mlxsw_sp->port_type_speed_ops;
	port_type_speed_ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl,
					       mlxsw_sp_port->local_port, 0,
					       false);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;
	port_type_speed_ops->reg_ptys_eth_unpack(mlxsw_sp, ptys_pl, NULL, NULL,
						 &eth_proto_oper);
	*speed = port_type_speed_ops->from_ptys_speed(mlxsw_sp, eth_proto_oper);
	return 0;
}

int mlxsw_sp_port_ets_set(struct mlxsw_sp_port *mlxsw_sp_port,
			  enum mlxsw_reg_qeec_hr hr, u8 index, u8 next_index,
			  bool dwrr, u8 dwrr_weight)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qeec_pl[MLXSW_REG_QEEC_LEN];

	mlxsw_reg_qeec_pack(qeec_pl, mlxsw_sp_port->local_port, hr, index,
			    next_index);
	mlxsw_reg_qeec_de_set(qeec_pl, true);
	mlxsw_reg_qeec_dwrr_set(qeec_pl, dwrr);
	mlxsw_reg_qeec_dwrr_weight_set(qeec_pl, dwrr_weight);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qeec), qeec_pl);
}

int mlxsw_sp_port_ets_maxrate_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  enum mlxsw_reg_qeec_hr hr, u8 index,
				  u8 next_index, u32 maxrate, u8 burst_size)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qeec_pl[MLXSW_REG_QEEC_LEN];

	mlxsw_reg_qeec_pack(qeec_pl, mlxsw_sp_port->local_port, hr, index,
			    next_index);
	mlxsw_reg_qeec_mase_set(qeec_pl, true);
	mlxsw_reg_qeec_max_shaper_rate_set(qeec_pl, maxrate);
	mlxsw_reg_qeec_max_shaper_bs_set(qeec_pl, burst_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qeec), qeec_pl);
}

static int mlxsw_sp_port_min_bw_set(struct mlxsw_sp_port *mlxsw_sp_port,
				    enum mlxsw_reg_qeec_hr hr, u8 index,
				    u8 next_index, u32 minrate)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qeec_pl[MLXSW_REG_QEEC_LEN];

	mlxsw_reg_qeec_pack(qeec_pl, mlxsw_sp_port->local_port, hr, index,
			    next_index);
	mlxsw_reg_qeec_mise_set(qeec_pl, true);
	mlxsw_reg_qeec_min_shaper_rate_set(qeec_pl, minrate);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qeec), qeec_pl);
}

int mlxsw_sp_port_prio_tc_set(struct mlxsw_sp_port *mlxsw_sp_port,
			      u8 switch_prio, u8 tclass)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qtct_pl[MLXSW_REG_QTCT_LEN];

	mlxsw_reg_qtct_pack(qtct_pl, mlxsw_sp_port->local_port, switch_prio,
			    tclass);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qtct), qtct_pl);
}

static int mlxsw_sp_port_ets_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err, i;

	/* Setup the elements hierarcy, so that each TC is linked to
	 * one subgroup, which are all member in the same group.
	 */
	err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
				    MLXSW_REG_QEEC_HR_GROUP, 0, 0, false, 0);
	if (err)
		return err;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HR_SUBGROUP, i,
					    0, false, 0);
		if (err)
			return err;
	}
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HR_TC, i, i,
					    false, 0);
		if (err)
			return err;

		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HR_TC,
					    i + 8, i,
					    true, 100);
		if (err)
			return err;
	}

	/* Make sure the max shaper is disabled in all hierarchies that support
	 * it. Note that this disables ptps (PTP shaper), but that is intended
	 * for the initial configuration.
	 */
	err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HR_PORT, 0, 0,
					    MLXSW_REG_QEEC_MAS_DIS, 0);
	if (err)
		return err;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
						    MLXSW_REG_QEEC_HR_SUBGROUP,
						    i, 0,
						    MLXSW_REG_QEEC_MAS_DIS, 0);
		if (err)
			return err;
	}
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
						    MLXSW_REG_QEEC_HR_TC,
						    i, i,
						    MLXSW_REG_QEEC_MAS_DIS, 0);
		if (err)
			return err;

		err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
						    MLXSW_REG_QEEC_HR_TC,
						    i + 8, i,
						    MLXSW_REG_QEEC_MAS_DIS, 0);
		if (err)
			return err;
	}

	/* Configure the min shaper for multicast TCs. */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_min_bw_set(mlxsw_sp_port,
					       MLXSW_REG_QEEC_HR_TC,
					       i + 8, i,
					       MLXSW_REG_QEEC_MIS_MIN);
		if (err)
			return err;
	}

	/* Map all priorities to traffic class 0. */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_prio_tc_set(mlxsw_sp_port, i, 0);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_port_tc_mc_mode_set(struct mlxsw_sp_port *mlxsw_sp_port,
					bool enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qtctm_pl[MLXSW_REG_QTCTM_LEN];

	mlxsw_reg_qtctm_pack(qtctm_pl, mlxsw_sp_port->local_port, enable);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qtctm), qtctm_pl);
}

static int mlxsw_sp_port_create(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				u8 split_base_local_port,
				struct mlxsw_sp_port_mapping *port_mapping)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	bool split = !!split_base_local_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct net_device *dev;
	int err;

	err = mlxsw_core_port_init(mlxsw_sp->core, local_port,
				   port_mapping->module + 1, split,
				   port_mapping->lane / port_mapping->width,
				   mlxsw_sp->base_mac,
				   sizeof(mlxsw_sp->base_mac));
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to init core port\n",
			local_port);
		return err;
	}

	dev = alloc_etherdev(sizeof(struct mlxsw_sp_port));
	if (!dev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}
	SET_NETDEV_DEV(dev, mlxsw_sp->bus_info->dev);
	dev_net_set(dev, mlxsw_sp_net(mlxsw_sp));
	mlxsw_sp_port = netdev_priv(dev);
	mlxsw_sp_port->dev = dev;
	mlxsw_sp_port->mlxsw_sp = mlxsw_sp;
	mlxsw_sp_port->local_port = local_port;
	mlxsw_sp_port->pvid = MLXSW_SP_DEFAULT_VID;
	mlxsw_sp_port->split = split;
	mlxsw_sp_port->split_base_local_port = split_base_local_port;
	mlxsw_sp_port->mapping = *port_mapping;
	mlxsw_sp_port->link.autoneg = 1;
	INIT_LIST_HEAD(&mlxsw_sp_port->vlans_list);

	mlxsw_sp_port->pcpu_stats =
		netdev_alloc_pcpu_stats(struct mlxsw_sp_port_pcpu_stats);
	if (!mlxsw_sp_port->pcpu_stats) {
		err = -ENOMEM;
		goto err_alloc_stats;
	}

	INIT_DELAYED_WORK(&mlxsw_sp_port->periodic_hw_stats.update_dw,
			  &update_stats_cache);

	dev->netdev_ops = &mlxsw_sp_port_netdev_ops;
	dev->ethtool_ops = &mlxsw_sp_port_ethtool_ops;

	err = mlxsw_sp_port_module_map(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to map module\n",
			mlxsw_sp_port->local_port);
		goto err_port_module_map;
	}

	err = mlxsw_sp_port_swid_set(mlxsw_sp_port, 0);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set SWID\n",
			mlxsw_sp_port->local_port);
		goto err_port_swid_set;
	}

	err = mlxsw_sp_port_dev_addr_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Unable to init port mac address\n",
			mlxsw_sp_port->local_port);
		goto err_dev_addr_init;
	}

	netif_carrier_off(dev);

	dev->features |= NETIF_F_NETNS_LOCAL | NETIF_F_LLTX | NETIF_F_SG |
			 NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_TC;
	dev->hw_features |= NETIF_F_HW_TC | NETIF_F_LOOPBACK;

	dev->min_mtu = 0;
	dev->max_mtu = ETH_MAX_MTU;

	/* Each packet needs to have a Tx header (metadata) on top all other
	 * headers.
	 */
	dev->needed_headroom = MLXSW_TXHDR_LEN;

	err = mlxsw_sp_port_system_port_mapping_set(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set system port mapping\n",
			mlxsw_sp_port->local_port);
		goto err_port_system_port_mapping_set;
	}

	err = mlxsw_sp_port_speed_by_width_set(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to enable speeds\n",
			mlxsw_sp_port->local_port);
		goto err_port_speed_by_width_set;
	}

	err = mlxsw_sp_port_mtu_set(mlxsw_sp_port, ETH_DATA_LEN);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set MTU\n",
			mlxsw_sp_port->local_port);
		goto err_port_mtu_set;
	}

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
	if (err)
		goto err_port_admin_status_set;

	err = mlxsw_sp_port_buffers_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize buffers\n",
			mlxsw_sp_port->local_port);
		goto err_port_buffers_init;
	}

	err = mlxsw_sp_port_ets_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize ETS\n",
			mlxsw_sp_port->local_port);
		goto err_port_ets_init;
	}

	err = mlxsw_sp_port_tc_mc_mode_set(mlxsw_sp_port, true);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize TC MC mode\n",
			mlxsw_sp_port->local_port);
		goto err_port_tc_mc_mode;
	}

	/* ETS and buffers must be initialized before DCB. */
	err = mlxsw_sp_port_dcb_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize DCB\n",
			mlxsw_sp_port->local_port);
		goto err_port_dcb_init;
	}

	err = mlxsw_sp_port_fids_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize FIDs\n",
			mlxsw_sp_port->local_port);
		goto err_port_fids_init;
	}

	err = mlxsw_sp_tc_qdisc_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize TC qdiscs\n",
			mlxsw_sp_port->local_port);
		goto err_port_qdiscs_init;
	}

	err = mlxsw_sp_port_vlan_set(mlxsw_sp_port, 0, VLAN_N_VID - 1, false,
				     false);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to clear VLAN filter\n",
			mlxsw_sp_port->local_port);
		goto err_port_vlan_clear;
	}

	err = mlxsw_sp_port_nve_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize NVE\n",
			mlxsw_sp_port->local_port);
		goto err_port_nve_init;
	}

	err = mlxsw_sp_port_pvid_set(mlxsw_sp_port, MLXSW_SP_DEFAULT_VID);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set PVID\n",
			mlxsw_sp_port->local_port);
		goto err_port_pvid_set;
	}

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_create(mlxsw_sp_port,
						       MLXSW_SP_DEFAULT_VID);
	if (IS_ERR(mlxsw_sp_port_vlan)) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to create VID 1\n",
			mlxsw_sp_port->local_port);
		err = PTR_ERR(mlxsw_sp_port_vlan);
		goto err_port_vlan_create;
	}
	mlxsw_sp_port->default_vlan = mlxsw_sp_port_vlan;

	INIT_DELAYED_WORK(&mlxsw_sp_port->ptp.shaper_dw,
			  mlxsw_sp->ptp_ops->shaper_work);
	INIT_DELAYED_WORK(&mlxsw_sp_port->span.speed_update_dw,
			  mlxsw_sp_span_speed_update_work);

	mlxsw_sp->ports[local_port] = mlxsw_sp_port;
	err = register_netdev(dev);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to register netdev\n",
			mlxsw_sp_port->local_port);
		goto err_register_netdev;
	}

	mlxsw_core_port_eth_set(mlxsw_sp->core, mlxsw_sp_port->local_port,
				mlxsw_sp_port, dev);
	mlxsw_core_schedule_dw(&mlxsw_sp_port->periodic_hw_stats.update_dw, 0);
	return 0;

err_register_netdev:
	mlxsw_sp->ports[local_port] = NULL;
	mlxsw_sp_port_vlan_destroy(mlxsw_sp_port_vlan);
err_port_vlan_create:
err_port_pvid_set:
	mlxsw_sp_port_nve_fini(mlxsw_sp_port);
err_port_nve_init:
err_port_vlan_clear:
	mlxsw_sp_tc_qdisc_fini(mlxsw_sp_port);
err_port_qdiscs_init:
	mlxsw_sp_port_fids_fini(mlxsw_sp_port);
err_port_fids_init:
	mlxsw_sp_port_dcb_fini(mlxsw_sp_port);
err_port_dcb_init:
	mlxsw_sp_port_tc_mc_mode_set(mlxsw_sp_port, false);
err_port_tc_mc_mode:
err_port_ets_init:
err_port_buffers_init:
err_port_admin_status_set:
err_port_mtu_set:
err_port_speed_by_width_set:
err_port_system_port_mapping_set:
err_dev_addr_init:
	mlxsw_sp_port_swid_set(mlxsw_sp_port, MLXSW_PORT_SWID_DISABLED_PORT);
err_port_swid_set:
	mlxsw_sp_port_module_unmap(mlxsw_sp_port);
err_port_module_map:
	free_percpu(mlxsw_sp_port->pcpu_stats);
err_alloc_stats:
	free_netdev(dev);
err_alloc_etherdev:
	mlxsw_core_port_fini(mlxsw_sp->core, local_port);
	return err;
}

static void mlxsw_sp_port_remove(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp->ports[local_port];

	cancel_delayed_work_sync(&mlxsw_sp_port->periodic_hw_stats.update_dw);
	cancel_delayed_work_sync(&mlxsw_sp_port->span.speed_update_dw);
	cancel_delayed_work_sync(&mlxsw_sp_port->ptp.shaper_dw);
	mlxsw_sp_port_ptp_clear(mlxsw_sp_port);
	mlxsw_core_port_clear(mlxsw_sp->core, local_port, mlxsw_sp);
	unregister_netdev(mlxsw_sp_port->dev); /* This calls ndo_stop */
	mlxsw_sp->ports[local_port] = NULL;
	mlxsw_sp_port_vlan_flush(mlxsw_sp_port, true);
	mlxsw_sp_port_nve_fini(mlxsw_sp_port);
	mlxsw_sp_tc_qdisc_fini(mlxsw_sp_port);
	mlxsw_sp_port_fids_fini(mlxsw_sp_port);
	mlxsw_sp_port_dcb_fini(mlxsw_sp_port);
	mlxsw_sp_port_tc_mc_mode_set(mlxsw_sp_port, false);
	mlxsw_sp_port_swid_set(mlxsw_sp_port, MLXSW_PORT_SWID_DISABLED_PORT);
	mlxsw_sp_port_module_unmap(mlxsw_sp_port);
	free_percpu(mlxsw_sp_port->pcpu_stats);
	WARN_ON_ONCE(!list_empty(&mlxsw_sp_port->vlans_list));
	free_netdev(mlxsw_sp_port->dev);
	mlxsw_core_port_fini(mlxsw_sp->core, local_port);
}

static int mlxsw_sp_cpu_port_create(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	mlxsw_sp_port = kzalloc(sizeof(*mlxsw_sp_port), GFP_KERNEL);
	if (!mlxsw_sp_port)
		return -ENOMEM;

	mlxsw_sp_port->mlxsw_sp = mlxsw_sp;
	mlxsw_sp_port->local_port = MLXSW_PORT_CPU_PORT;

	err = mlxsw_core_cpu_port_init(mlxsw_sp->core,
				       mlxsw_sp_port,
				       mlxsw_sp->base_mac,
				       sizeof(mlxsw_sp->base_mac));
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize core CPU port\n");
		goto err_core_cpu_port_init;
	}

	mlxsw_sp->ports[MLXSW_PORT_CPU_PORT] = mlxsw_sp_port;
	return 0;

err_core_cpu_port_init:
	kfree(mlxsw_sp_port);
	return err;
}

static void mlxsw_sp_cpu_port_remove(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
				mlxsw_sp->ports[MLXSW_PORT_CPU_PORT];

	mlxsw_core_cpu_port_fini(mlxsw_sp->core);
	mlxsw_sp->ports[MLXSW_PORT_CPU_PORT] = NULL;
	kfree(mlxsw_sp_port);
}

static bool mlxsw_sp_port_created(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	return mlxsw_sp->ports[local_port] != NULL;
}

static void mlxsw_sp_ports_remove(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 1; i < mlxsw_core_max_ports(mlxsw_sp->core); i++)
		if (mlxsw_sp_port_created(mlxsw_sp, i))
			mlxsw_sp_port_remove(mlxsw_sp, i);
	mlxsw_sp_cpu_port_remove(mlxsw_sp);
	kfree(mlxsw_sp->ports);
	mlxsw_sp->ports = NULL;
}

static int mlxsw_sp_ports_create(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_sp->core);
	struct mlxsw_sp_port_mapping *port_mapping;
	size_t alloc_size;
	int i;
	int err;

	alloc_size = sizeof(struct mlxsw_sp_port *) * max_ports;
	mlxsw_sp->ports = kzalloc(alloc_size, GFP_KERNEL);
	if (!mlxsw_sp->ports)
		return -ENOMEM;

	err = mlxsw_sp_cpu_port_create(mlxsw_sp);
	if (err)
		goto err_cpu_port_create;

	for (i = 1; i < max_ports; i++) {
		port_mapping = mlxsw_sp->port_mapping[i];
		if (!port_mapping)
			continue;
		err = mlxsw_sp_port_create(mlxsw_sp, i, 0, port_mapping);
		if (err)
			goto err_port_create;
	}
	return 0;

err_port_create:
	for (i--; i >= 1; i--)
		if (mlxsw_sp_port_created(mlxsw_sp, i))
			mlxsw_sp_port_remove(mlxsw_sp, i);
	mlxsw_sp_cpu_port_remove(mlxsw_sp);
err_cpu_port_create:
	kfree(mlxsw_sp->ports);
	mlxsw_sp->ports = NULL;
	return err;
}

static int mlxsw_sp_port_module_info_init(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_sp->core);
	struct mlxsw_sp_port_mapping port_mapping;
	int i;
	int err;

	mlxsw_sp->port_mapping = kcalloc(max_ports,
					 sizeof(struct mlxsw_sp_port_mapping *),
					 GFP_KERNEL);
	if (!mlxsw_sp->port_mapping)
		return -ENOMEM;

	for (i = 1; i < max_ports; i++) {
		err = mlxsw_sp_port_module_info_get(mlxsw_sp, i, &port_mapping);
		if (err)
			goto err_port_module_info_get;
		if (!port_mapping.width)
			continue;

		mlxsw_sp->port_mapping[i] = kmemdup(&port_mapping,
						    sizeof(port_mapping),
						    GFP_KERNEL);
		if (!mlxsw_sp->port_mapping[i]) {
			err = -ENOMEM;
			goto err_port_module_info_dup;
		}
	}
	return 0;

err_port_module_info_get:
err_port_module_info_dup:
	for (i--; i >= 1; i--)
		kfree(mlxsw_sp->port_mapping[i]);
	kfree(mlxsw_sp->port_mapping);
	return err;
}

static void mlxsw_sp_port_module_info_fini(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 1; i < mlxsw_core_max_ports(mlxsw_sp->core); i++)
		kfree(mlxsw_sp->port_mapping[i]);
	kfree(mlxsw_sp->port_mapping);
}

static u8 mlxsw_sp_cluster_base_port_get(u8 local_port, unsigned int max_width)
{
	u8 offset = (local_port - 1) % max_width;

	return local_port - offset;
}

static int
mlxsw_sp_port_split_create(struct mlxsw_sp *mlxsw_sp, u8 base_port,
			   struct mlxsw_sp_port_mapping *port_mapping,
			   unsigned int count, u8 offset)
{
	struct mlxsw_sp_port_mapping split_port_mapping;
	int err, i;

	split_port_mapping = *port_mapping;
	split_port_mapping.width /= count;
	for (i = 0; i < count; i++) {
		err = mlxsw_sp_port_create(mlxsw_sp, base_port + i * offset,
					   base_port, &split_port_mapping);
		if (err)
			goto err_port_create;
		split_port_mapping.lane += split_port_mapping.width;
	}

	return 0;

err_port_create:
	for (i--; i >= 0; i--)
		if (mlxsw_sp_port_created(mlxsw_sp, base_port + i * offset))
			mlxsw_sp_port_remove(mlxsw_sp, base_port + i * offset);
	return err;
}

static void mlxsw_sp_port_unsplit_create(struct mlxsw_sp *mlxsw_sp,
					 u8 base_port,
					 unsigned int count, u8 offset)
{
	struct mlxsw_sp_port_mapping *port_mapping;
	int i;

	/* Go over original unsplit ports in the gap and recreate them. */
	for (i = 0; i < count * offset; i++) {
		port_mapping = mlxsw_sp->port_mapping[base_port + i];
		if (!port_mapping)
			continue;
		mlxsw_sp_port_create(mlxsw_sp, base_port + i, 0, port_mapping);
	}
}

static int mlxsw_sp_local_ports_offset(struct mlxsw_core *mlxsw_core,
				       unsigned int count,
				       unsigned int max_width)
{
	enum mlxsw_res_id local_ports_in_x_res_id;
	int split_width = max_width / count;

	if (split_width == 1)
		local_ports_in_x_res_id = MLXSW_RES_ID_LOCAL_PORTS_IN_1X;
	else if (split_width == 2)
		local_ports_in_x_res_id = MLXSW_RES_ID_LOCAL_PORTS_IN_2X;
	else if (split_width == 4)
		local_ports_in_x_res_id = MLXSW_RES_ID_LOCAL_PORTS_IN_4X;
	else
		return -EINVAL;

	if (!mlxsw_core_res_valid(mlxsw_core, local_ports_in_x_res_id))
		return -EINVAL;
	return mlxsw_core_res_get(mlxsw_core, local_ports_in_x_res_id);
}

static struct mlxsw_sp_port *
mlxsw_sp_port_get_by_local_port(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	if (mlxsw_sp->ports && mlxsw_sp->ports[local_port])
		return mlxsw_sp->ports[local_port];
	return NULL;
}

static int mlxsw_sp_port_split(struct mlxsw_core *mlxsw_core, u8 local_port,
			       unsigned int count,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_port_mapping port_mapping;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int max_width;
	u8 base_port;
	int offset;
	int i;
	int err;

	mlxsw_sp_port = mlxsw_sp_port_get_by_local_port(mlxsw_sp, local_port);
	if (!mlxsw_sp_port) {
		dev_err(mlxsw_sp->bus_info->dev, "Port number \"%d\" does not exist\n",
			local_port);
		NL_SET_ERR_MSG_MOD(extack, "Port number does not exist");
		return -EINVAL;
	}

	/* Split ports cannot be split. */
	if (mlxsw_sp_port->split) {
		netdev_err(mlxsw_sp_port->dev, "Port cannot be split further\n");
		NL_SET_ERR_MSG_MOD(extack, "Port cannot be split further");
		return -EINVAL;
	}

	max_width = mlxsw_core_module_max_width(mlxsw_core,
						mlxsw_sp_port->mapping.module);
	if (max_width < 0) {
		netdev_err(mlxsw_sp_port->dev, "Cannot get max width of port module\n");
		NL_SET_ERR_MSG_MOD(extack, "Cannot get max width of port module");
		return max_width;
	}

	/* Split port with non-max and 1 module width cannot be split. */
	if (mlxsw_sp_port->mapping.width != max_width || max_width == 1) {
		netdev_err(mlxsw_sp_port->dev, "Port cannot be split\n");
		NL_SET_ERR_MSG_MOD(extack, "Port cannot be split");
		return -EINVAL;
	}

	if (count == 1 || !is_power_of_2(count) || count > max_width) {
		netdev_err(mlxsw_sp_port->dev, "Invalid split count\n");
		NL_SET_ERR_MSG_MOD(extack, "Invalid split count");
		return -EINVAL;
	}

	offset = mlxsw_sp_local_ports_offset(mlxsw_core, count, max_width);
	if (offset < 0) {
		netdev_err(mlxsw_sp_port->dev, "Cannot obtain local port offset\n");
		NL_SET_ERR_MSG_MOD(extack, "Cannot obtain local port offset");
		return -EINVAL;
	}

	/* Only in case max split is being done, the local port and
	 * base port may differ.
	 */
	base_port = count == max_width ?
		    mlxsw_sp_cluster_base_port_get(local_port, max_width) :
		    local_port;

	for (i = 0; i < count * offset; i++) {
		/* Expect base port to exist and also the one in the middle in
		 * case of maximal split count.
		 */
		if (i == 0 || (count == max_width && i == count / 2))
			continue;

		if (mlxsw_sp_port_created(mlxsw_sp, base_port + i)) {
			netdev_err(mlxsw_sp_port->dev, "Invalid split configuration\n");
			NL_SET_ERR_MSG_MOD(extack, "Invalid split configuration");
			return -EINVAL;
		}
	}

	port_mapping = mlxsw_sp_port->mapping;

	for (i = 0; i < count; i++)
		if (mlxsw_sp_port_created(mlxsw_sp, base_port + i * offset))
			mlxsw_sp_port_remove(mlxsw_sp, base_port + i * offset);

	err = mlxsw_sp_port_split_create(mlxsw_sp, base_port, &port_mapping,
					 count, offset);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to create split ports\n");
		goto err_port_split_create;
	}

	return 0;

err_port_split_create:
	mlxsw_sp_port_unsplit_create(mlxsw_sp, base_port, count, offset);
	return err;
}

static int mlxsw_sp_port_unsplit(struct mlxsw_core *mlxsw_core, u8 local_port,
				 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_port *mlxsw_sp_port;
	unsigned int count;
	int max_width;
	u8 base_port;
	int offset;
	int i;

	mlxsw_sp_port = mlxsw_sp_port_get_by_local_port(mlxsw_sp, local_port);
	if (!mlxsw_sp_port) {
		dev_err(mlxsw_sp->bus_info->dev, "Port number \"%d\" does not exist\n",
			local_port);
		NL_SET_ERR_MSG_MOD(extack, "Port number does not exist");
		return -EINVAL;
	}

	if (!mlxsw_sp_port->split) {
		netdev_err(mlxsw_sp_port->dev, "Port was not split\n");
		NL_SET_ERR_MSG_MOD(extack, "Port was not split");
		return -EINVAL;
	}

	max_width = mlxsw_core_module_max_width(mlxsw_core,
						mlxsw_sp_port->mapping.module);
	if (max_width < 0) {
		netdev_err(mlxsw_sp_port->dev, "Cannot get max width of port module\n");
		NL_SET_ERR_MSG_MOD(extack, "Cannot get max width of port module");
		return max_width;
	}

	count = max_width / mlxsw_sp_port->mapping.width;

	offset = mlxsw_sp_local_ports_offset(mlxsw_core, count, max_width);
	if (WARN_ON(offset < 0)) {
		netdev_err(mlxsw_sp_port->dev, "Cannot obtain local port offset\n");
		NL_SET_ERR_MSG_MOD(extack, "Cannot obtain local port offset");
		return -EINVAL;
	}

	base_port = mlxsw_sp_port->split_base_local_port;

	for (i = 0; i < count; i++)
		if (mlxsw_sp_port_created(mlxsw_sp, base_port + i * offset))
			mlxsw_sp_port_remove(mlxsw_sp, base_port + i * offset);

	mlxsw_sp_port_unsplit_create(mlxsw_sp, base_port, count, offset);

	return 0;
}

static void
mlxsw_sp_port_down_wipe_counters(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int i;

	for (i = 0; i < TC_MAX_QUEUE; i++)
		mlxsw_sp_port->periodic_hw_stats.xstats.backlog[i] = 0;
}

static void mlxsw_sp_pude_event_func(const struct mlxsw_reg_info *reg,
				     char *pude_pl, void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port;
	enum mlxsw_reg_pude_oper_status status;
	u8 local_port;

	local_port = mlxsw_reg_pude_local_port_get(pude_pl);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port)
		return;

	status = mlxsw_reg_pude_oper_status_get(pude_pl);
	if (status == MLXSW_PORT_OPER_STATUS_UP) {
		netdev_info(mlxsw_sp_port->dev, "link up\n");
		netif_carrier_on(mlxsw_sp_port->dev);
		mlxsw_core_schedule_dw(&mlxsw_sp_port->ptp.shaper_dw, 0);
		mlxsw_core_schedule_dw(&mlxsw_sp_port->span.speed_update_dw, 0);
	} else {
		netdev_info(mlxsw_sp_port->dev, "link down\n");
		netif_carrier_off(mlxsw_sp_port->dev);
		mlxsw_sp_port_down_wipe_counters(mlxsw_sp_port);
	}
}

static void mlxsw_sp1_ptp_fifo_event_func(struct mlxsw_sp *mlxsw_sp,
					  char *mtpptr_pl, bool ingress)
{
	u8 local_port;
	u8 num_rec;
	int i;

	local_port = mlxsw_reg_mtpptr_local_port_get(mtpptr_pl);
	num_rec = mlxsw_reg_mtpptr_num_rec_get(mtpptr_pl);
	for (i = 0; i < num_rec; i++) {
		u8 domain_number;
		u8 message_type;
		u16 sequence_id;
		u64 timestamp;

		mlxsw_reg_mtpptr_unpack(mtpptr_pl, i, &message_type,
					&domain_number, &sequence_id,
					&timestamp);
		mlxsw_sp1_ptp_got_timestamp(mlxsw_sp, ingress, local_port,
					    message_type, domain_number,
					    sequence_id, timestamp);
	}
}

static void mlxsw_sp1_ptp_ing_fifo_event_func(const struct mlxsw_reg_info *reg,
					      char *mtpptr_pl, void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp1_ptp_fifo_event_func(mlxsw_sp, mtpptr_pl, true);
}

static void mlxsw_sp1_ptp_egr_fifo_event_func(const struct mlxsw_reg_info *reg,
					      char *mtpptr_pl, void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp1_ptp_fifo_event_func(mlxsw_sp, mtpptr_pl, false);
}

void mlxsw_sp_rx_listener_no_mark_func(struct sk_buff *skb,
				       u8 local_port, void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp->ports[local_port];
	struct mlxsw_sp_port_pcpu_stats *pcpu_stats;

	if (unlikely(!mlxsw_sp_port)) {
		dev_warn_ratelimited(mlxsw_sp->bus_info->dev, "Port %d: skb received for non-existent port\n",
				     local_port);
		return;
	}

	skb->dev = mlxsw_sp_port->dev;

	pcpu_stats = this_cpu_ptr(mlxsw_sp_port->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->rx_packets++;
	pcpu_stats->rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_stats->syncp);

	skb->protocol = eth_type_trans(skb, skb->dev);
	netif_receive_skb(skb);
}

static void mlxsw_sp_rx_listener_mark_func(struct sk_buff *skb, u8 local_port,
					   void *priv)
{
	skb->offload_fwd_mark = 1;
	return mlxsw_sp_rx_listener_no_mark_func(skb, local_port, priv);
}

static void mlxsw_sp_rx_listener_l3_mark_func(struct sk_buff *skb,
					      u8 local_port, void *priv)
{
	skb->offload_l3_fwd_mark = 1;
	skb->offload_fwd_mark = 1;
	return mlxsw_sp_rx_listener_no_mark_func(skb, local_port, priv);
}

void mlxsw_sp_ptp_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			  u8 local_port)
{
	mlxsw_sp->ptp_ops->receive(mlxsw_sp, skb, local_port);
}

void mlxsw_sp_sample_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			     u8 local_port)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp->ports[local_port];
	struct mlxsw_sp_port_sample *sample;
	u32 size;

	if (unlikely(!mlxsw_sp_port)) {
		dev_warn_ratelimited(mlxsw_sp->bus_info->dev, "Port %d: sample skb received for non-existent port\n",
				     local_port);
		goto out;
	}

	rcu_read_lock();
	sample = rcu_dereference(mlxsw_sp_port->sample);
	if (!sample)
		goto out_unlock;
	size = sample->truncate ? sample->trunc_size : skb->len;
	psample_sample_packet(sample->psample_group, skb, size,
			      mlxsw_sp_port->dev->ifindex, 0, sample->rate);
out_unlock:
	rcu_read_unlock();
out:
	consume_skb(skb);
}

#define MLXSW_SP_RXL_NO_MARK(_trap_id, _action, _trap_group, _is_ctrl)	\
	MLXSW_RXL(mlxsw_sp_rx_listener_no_mark_func, _trap_id, _action,	\
		  _is_ctrl, SP_##_trap_group, DISCARD)

#define MLXSW_SP_RXL_MARK(_trap_id, _action, _trap_group, _is_ctrl)	\
	MLXSW_RXL(mlxsw_sp_rx_listener_mark_func, _trap_id, _action,	\
		_is_ctrl, SP_##_trap_group, DISCARD)

#define MLXSW_SP_RXL_L3_MARK(_trap_id, _action, _trap_group, _is_ctrl)	\
	MLXSW_RXL(mlxsw_sp_rx_listener_l3_mark_func, _trap_id, _action,	\
		_is_ctrl, SP_##_trap_group, DISCARD)

#define MLXSW_SP_EVENTL(_func, _trap_id)		\
	MLXSW_EVENTL(_func, _trap_id, SP_EVENT)

static const struct mlxsw_listener mlxsw_sp_listener[] = {
	/* Events */
	MLXSW_SP_EVENTL(mlxsw_sp_pude_event_func, PUDE),
	/* L2 traps */
	MLXSW_SP_RXL_NO_MARK(FID_MISS, TRAP_TO_CPU, FID_MISS, false),
	/* L3 traps */
	MLXSW_SP_RXL_MARK(IPV6_UNSPECIFIED_ADDRESS, TRAP_TO_CPU, ROUTER_EXP,
			  false),
	MLXSW_SP_RXL_MARK(IPV6_LINK_LOCAL_SRC, TRAP_TO_CPU, ROUTER_EXP, false),
	MLXSW_SP_RXL_MARK(IPV6_MC_LINK_LOCAL_DEST, TRAP_TO_CPU, ROUTER_EXP,
			  false),
	MLXSW_SP_RXL_NO_MARK(DISCARD_ING_ROUTER_SIP_CLASS_E, FORWARD,
			     ROUTER_EXP, false),
	MLXSW_SP_RXL_NO_MARK(DISCARD_ING_ROUTER_MC_DMAC, FORWARD,
			     ROUTER_EXP, false),
	MLXSW_SP_RXL_NO_MARK(DISCARD_ING_ROUTER_SIP_DIP, FORWARD,
			     ROUTER_EXP, false),
	MLXSW_SP_RXL_NO_MARK(DISCARD_ING_ROUTER_DIP_LINK_LOCAL, FORWARD,
			     ROUTER_EXP, false),
	/* Multicast Router Traps */
	MLXSW_SP_RXL_MARK(ACL1, TRAP_TO_CPU, MULTICAST, false),
	MLXSW_SP_RXL_L3_MARK(ACL2, TRAP_TO_CPU, MULTICAST, false),
	/* NVE traps */
	MLXSW_SP_RXL_MARK(NVE_ENCAP_ARP, TRAP_TO_CPU, NEIGH_DISCOVERY, false),
};

static const struct mlxsw_listener mlxsw_sp1_listener[] = {
	/* Events */
	MLXSW_EVENTL(mlxsw_sp1_ptp_egr_fifo_event_func, PTP_EGR_FIFO, SP_PTP0),
	MLXSW_EVENTL(mlxsw_sp1_ptp_ing_fifo_event_func, PTP_ING_FIFO, SP_PTP0),
};

static int mlxsw_sp_cpu_policers_set(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	char qpcr_pl[MLXSW_REG_QPCR_LEN];
	enum mlxsw_reg_qpcr_ir_units ir_units;
	int max_cpu_policers;
	bool is_bytes;
	u8 burst_size;
	u32 rate;
	int i, err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_core, MAX_CPU_POLICERS))
		return -EIO;

	max_cpu_policers = MLXSW_CORE_RES_GET(mlxsw_core, MAX_CPU_POLICERS);

	ir_units = MLXSW_REG_QPCR_IR_UNITS_M;
	for (i = 0; i < max_cpu_policers; i++) {
		is_bytes = false;
		switch (i) {
		case MLXSW_REG_HTGT_TRAP_GROUP_SP_ROUTER_EXP:
		case MLXSW_REG_HTGT_TRAP_GROUP_SP_MULTICAST:
		case MLXSW_REG_HTGT_TRAP_GROUP_SP_FID_MISS:
			rate = 1024;
			burst_size = 7;
			break;
		default:
			continue;
		}

		__set_bit(i, mlxsw_sp->trap->policers_usage);
		mlxsw_reg_qpcr_pack(qpcr_pl, i, ir_units, is_bytes, rate,
				    burst_size);
		err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(qpcr), qpcr_pl);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_trap_groups_set(struct mlxsw_core *mlxsw_core)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];
	enum mlxsw_reg_htgt_trap_group i;
	int max_cpu_policers;
	int max_trap_groups;
	u8 priority, tc;
	u16 policer_id;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_core, MAX_TRAP_GROUPS))
		return -EIO;

	max_trap_groups = MLXSW_CORE_RES_GET(mlxsw_core, MAX_TRAP_GROUPS);
	max_cpu_policers = MLXSW_CORE_RES_GET(mlxsw_core, MAX_CPU_POLICERS);

	for (i = 0; i < max_trap_groups; i++) {
		policer_id = i;
		switch (i) {
		case MLXSW_REG_HTGT_TRAP_GROUP_SP_ROUTER_EXP:
		case MLXSW_REG_HTGT_TRAP_GROUP_SP_MULTICAST:
		case MLXSW_REG_HTGT_TRAP_GROUP_SP_FID_MISS:
			priority = 1;
			tc = 1;
			break;
		case MLXSW_REG_HTGT_TRAP_GROUP_SP_EVENT:
			priority = MLXSW_REG_HTGT_DEFAULT_PRIORITY;
			tc = MLXSW_REG_HTGT_DEFAULT_TC;
			policer_id = MLXSW_REG_HTGT_INVALID_POLICER;
			break;
		default:
			continue;
		}

		if (max_cpu_policers <= policer_id &&
		    policer_id != MLXSW_REG_HTGT_INVALID_POLICER)
			return -EIO;

		mlxsw_reg_htgt_pack(htgt_pl, i, policer_id, priority, tc);
		err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_traps_register(struct mlxsw_sp *mlxsw_sp,
				   const struct mlxsw_listener listeners[],
				   size_t listeners_count)
{
	int i;
	int err;

	for (i = 0; i < listeners_count; i++) {
		err = mlxsw_core_trap_register(mlxsw_sp->core,
					       &listeners[i],
					       mlxsw_sp);
		if (err)
			goto err_listener_register;

	}
	return 0;

err_listener_register:
	for (i--; i >= 0; i--) {
		mlxsw_core_trap_unregister(mlxsw_sp->core,
					   &listeners[i],
					   mlxsw_sp);
	}
	return err;
}

static void mlxsw_sp_traps_unregister(struct mlxsw_sp *mlxsw_sp,
				      const struct mlxsw_listener listeners[],
				      size_t listeners_count)
{
	int i;

	for (i = 0; i < listeners_count; i++) {
		mlxsw_core_trap_unregister(mlxsw_sp->core,
					   &listeners[i],
					   mlxsw_sp);
	}
}

static int mlxsw_sp_traps_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_trap *trap;
	u64 max_policers;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_CPU_POLICERS))
		return -EIO;
	max_policers = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_CPU_POLICERS);
	trap = kzalloc(struct_size(trap, policers_usage,
				   BITS_TO_LONGS(max_policers)), GFP_KERNEL);
	if (!trap)
		return -ENOMEM;
	trap->max_policers = max_policers;
	mlxsw_sp->trap = trap;

	err = mlxsw_sp_cpu_policers_set(mlxsw_sp->core);
	if (err)
		goto err_cpu_policers_set;

	err = mlxsw_sp_trap_groups_set(mlxsw_sp->core);
	if (err)
		goto err_trap_groups_set;

	err = mlxsw_sp_traps_register(mlxsw_sp, mlxsw_sp_listener,
				      ARRAY_SIZE(mlxsw_sp_listener));
	if (err)
		goto err_traps_register;

	err = mlxsw_sp_traps_register(mlxsw_sp, mlxsw_sp->listeners,
				      mlxsw_sp->listeners_count);
	if (err)
		goto err_extra_traps_init;

	return 0;

err_extra_traps_init:
	mlxsw_sp_traps_unregister(mlxsw_sp, mlxsw_sp_listener,
				  ARRAY_SIZE(mlxsw_sp_listener));
err_traps_register:
err_trap_groups_set:
err_cpu_policers_set:
	kfree(trap);
	return err;
}

static void mlxsw_sp_traps_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_traps_unregister(mlxsw_sp, mlxsw_sp->listeners,
				  mlxsw_sp->listeners_count);
	mlxsw_sp_traps_unregister(mlxsw_sp, mlxsw_sp_listener,
				  ARRAY_SIZE(mlxsw_sp_listener));
	kfree(mlxsw_sp->trap);
}

#define MLXSW_SP_LAG_SEED_INIT 0xcafecafe

static int mlxsw_sp_lag_init(struct mlxsw_sp *mlxsw_sp)
{
	char slcr_pl[MLXSW_REG_SLCR_LEN];
	u32 seed;
	int err;

	seed = jhash(mlxsw_sp->base_mac, sizeof(mlxsw_sp->base_mac),
		     MLXSW_SP_LAG_SEED_INIT);
	mlxsw_reg_slcr_pack(slcr_pl, MLXSW_REG_SLCR_LAG_HASH_SMAC |
				     MLXSW_REG_SLCR_LAG_HASH_DMAC |
				     MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE |
				     MLXSW_REG_SLCR_LAG_HASH_VLANID |
				     MLXSW_REG_SLCR_LAG_HASH_SIP |
				     MLXSW_REG_SLCR_LAG_HASH_DIP |
				     MLXSW_REG_SLCR_LAG_HASH_SPORT |
				     MLXSW_REG_SLCR_LAG_HASH_DPORT |
				     MLXSW_REG_SLCR_LAG_HASH_IPPROTO, seed);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcr), slcr_pl);
	if (err)
		return err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_LAG) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_LAG_MEMBERS))
		return -EIO;

	mlxsw_sp->lags = kcalloc(MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_LAG),
				 sizeof(struct mlxsw_sp_upper),
				 GFP_KERNEL);
	if (!mlxsw_sp->lags)
		return -ENOMEM;

	return 0;
}

static void mlxsw_sp_lag_fini(struct mlxsw_sp *mlxsw_sp)
{
	kfree(mlxsw_sp->lags);
}

static int mlxsw_sp_basic_trap_groups_set(struct mlxsw_core *mlxsw_core)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_EMAD,
			    MLXSW_REG_HTGT_INVALID_POLICER,
			    MLXSW_REG_HTGT_DEFAULT_PRIORITY,
			    MLXSW_REG_HTGT_DEFAULT_TC);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
}

static const struct mlxsw_sp_ptp_ops mlxsw_sp1_ptp_ops = {
	.clock_init	= mlxsw_sp1_ptp_clock_init,
	.clock_fini	= mlxsw_sp1_ptp_clock_fini,
	.init		= mlxsw_sp1_ptp_init,
	.fini		= mlxsw_sp1_ptp_fini,
	.receive	= mlxsw_sp1_ptp_receive,
	.transmitted	= mlxsw_sp1_ptp_transmitted,
	.hwtstamp_get	= mlxsw_sp1_ptp_hwtstamp_get,
	.hwtstamp_set	= mlxsw_sp1_ptp_hwtstamp_set,
	.shaper_work	= mlxsw_sp1_ptp_shaper_work,
	.get_ts_info	= mlxsw_sp1_ptp_get_ts_info,
	.get_stats_count = mlxsw_sp1_get_stats_count,
	.get_stats_strings = mlxsw_sp1_get_stats_strings,
	.get_stats	= mlxsw_sp1_get_stats,
};

static const struct mlxsw_sp_ptp_ops mlxsw_sp2_ptp_ops = {
	.clock_init	= mlxsw_sp2_ptp_clock_init,
	.clock_fini	= mlxsw_sp2_ptp_clock_fini,
	.init		= mlxsw_sp2_ptp_init,
	.fini		= mlxsw_sp2_ptp_fini,
	.receive	= mlxsw_sp2_ptp_receive,
	.transmitted	= mlxsw_sp2_ptp_transmitted,
	.hwtstamp_get	= mlxsw_sp2_ptp_hwtstamp_get,
	.hwtstamp_set	= mlxsw_sp2_ptp_hwtstamp_set,
	.shaper_work	= mlxsw_sp2_ptp_shaper_work,
	.get_ts_info	= mlxsw_sp2_ptp_get_ts_info,
	.get_stats_count = mlxsw_sp2_get_stats_count,
	.get_stats_strings = mlxsw_sp2_get_stats_strings,
	.get_stats	= mlxsw_sp2_get_stats,
};

static u32 mlxsw_sp1_span_buffsize_get(int mtu, u32 speed)
{
	return mtu * 5 / 2;
}

static const struct mlxsw_sp_span_ops mlxsw_sp1_span_ops = {
	.buffsize_get = mlxsw_sp1_span_buffsize_get,
};

#define MLXSW_SP2_SPAN_EG_MIRROR_BUFFER_FACTOR 38
#define MLXSW_SP3_SPAN_EG_MIRROR_BUFFER_FACTOR 50

static u32 __mlxsw_sp_span_buffsize_get(int mtu, u32 speed, u32 buffer_factor)
{
	return 3 * mtu + buffer_factor * speed / 1000;
}

static u32 mlxsw_sp2_span_buffsize_get(int mtu, u32 speed)
{
	int factor = MLXSW_SP2_SPAN_EG_MIRROR_BUFFER_FACTOR;

	return __mlxsw_sp_span_buffsize_get(mtu, speed, factor);
}

static const struct mlxsw_sp_span_ops mlxsw_sp2_span_ops = {
	.buffsize_get = mlxsw_sp2_span_buffsize_get,
};

static u32 mlxsw_sp3_span_buffsize_get(int mtu, u32 speed)
{
	int factor = MLXSW_SP3_SPAN_EG_MIRROR_BUFFER_FACTOR;

	return __mlxsw_sp_span_buffsize_get(mtu, speed, factor);
}

static const struct mlxsw_sp_span_ops mlxsw_sp3_span_ops = {
	.buffsize_get = mlxsw_sp3_span_buffsize_get,
};

u32 mlxsw_sp_span_buffsize_get(struct mlxsw_sp *mlxsw_sp, int mtu, u32 speed)
{
	u32 buffsize = mlxsw_sp->span_ops->buffsize_get(speed, mtu);

	return mlxsw_sp_bytes_cells(mlxsw_sp, buffsize) + 1;
}

static int mlxsw_sp_netdevice_event(struct notifier_block *unused,
				    unsigned long event, void *ptr);

static int mlxsw_sp_init(struct mlxsw_core *mlxsw_core,
			 const struct mlxsw_bus_info *mlxsw_bus_info,
			 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	int err;

	mlxsw_sp->core = mlxsw_core;
	mlxsw_sp->bus_info = mlxsw_bus_info;

	err = mlxsw_sp_fw_rev_validate(mlxsw_sp);
	if (err)
		return err;

	mlxsw_core_emad_string_tlv_enable(mlxsw_core);

	err = mlxsw_sp_base_mac_get(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to get base mac\n");
		return err;
	}

	err = mlxsw_sp_kvdl_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize KVDL\n");
		return err;
	}

	err = mlxsw_sp_fids_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize FIDs\n");
		goto err_fids_init;
	}

	err = mlxsw_sp_traps_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to set traps\n");
		goto err_traps_init;
	}

	err = mlxsw_sp_devlink_traps_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize devlink traps\n");
		goto err_devlink_traps_init;
	}

	err = mlxsw_sp_buffers_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize buffers\n");
		goto err_buffers_init;
	}

	err = mlxsw_sp_lag_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize LAG\n");
		goto err_lag_init;
	}

	/* Initialize SPAN before router and switchdev, so that those components
	 * can call mlxsw_sp_span_respin().
	 */
	err = mlxsw_sp_span_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to init span system\n");
		goto err_span_init;
	}

	err = mlxsw_sp_switchdev_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize switchdev\n");
		goto err_switchdev_init;
	}

	err = mlxsw_sp_counter_pool_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to init counter pool\n");
		goto err_counter_pool_init;
	}

	err = mlxsw_sp_afa_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize ACL actions\n");
		goto err_afa_init;
	}

	err = mlxsw_sp_nve_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize NVE\n");
		goto err_nve_init;
	}

	err = mlxsw_sp_acl_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize ACL\n");
		goto err_acl_init;
	}

	err = mlxsw_sp_router_init(mlxsw_sp, extack);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize router\n");
		goto err_router_init;
	}

	if (mlxsw_sp->bus_info->read_frc_capable) {
		/* NULL is a valid return value from clock_init */
		mlxsw_sp->clock =
			mlxsw_sp->ptp_ops->clock_init(mlxsw_sp,
						      mlxsw_sp->bus_info->dev);
		if (IS_ERR(mlxsw_sp->clock)) {
			err = PTR_ERR(mlxsw_sp->clock);
			dev_err(mlxsw_sp->bus_info->dev, "Failed to init ptp clock\n");
			goto err_ptp_clock_init;
		}
	}

	if (mlxsw_sp->clock) {
		/* NULL is a valid return value from ptp_ops->init */
		mlxsw_sp->ptp_state = mlxsw_sp->ptp_ops->init(mlxsw_sp);
		if (IS_ERR(mlxsw_sp->ptp_state)) {
			err = PTR_ERR(mlxsw_sp->ptp_state);
			dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize PTP\n");
			goto err_ptp_init;
		}
	}

	/* Initialize netdevice notifier after router and SPAN is initialized,
	 * so that the event handler can use router structures and call SPAN
	 * respin.
	 */
	mlxsw_sp->netdevice_nb.notifier_call = mlxsw_sp_netdevice_event;
	err = register_netdevice_notifier_net(mlxsw_sp_net(mlxsw_sp),
					      &mlxsw_sp->netdevice_nb);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to register netdev notifier\n");
		goto err_netdev_notifier;
	}

	err = mlxsw_sp_dpipe_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to init pipeline debug\n");
		goto err_dpipe_init;
	}

	err = mlxsw_sp_port_module_info_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to init port module info\n");
		goto err_port_module_info_init;
	}

	err = mlxsw_sp_ports_create(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to create ports\n");
		goto err_ports_create;
	}

	return 0;

err_ports_create:
	mlxsw_sp_port_module_info_fini(mlxsw_sp);
err_port_module_info_init:
	mlxsw_sp_dpipe_fini(mlxsw_sp);
err_dpipe_init:
	unregister_netdevice_notifier_net(mlxsw_sp_net(mlxsw_sp),
					  &mlxsw_sp->netdevice_nb);
err_netdev_notifier:
	if (mlxsw_sp->clock)
		mlxsw_sp->ptp_ops->fini(mlxsw_sp->ptp_state);
err_ptp_init:
	if (mlxsw_sp->clock)
		mlxsw_sp->ptp_ops->clock_fini(mlxsw_sp->clock);
err_ptp_clock_init:
	mlxsw_sp_router_fini(mlxsw_sp);
err_router_init:
	mlxsw_sp_acl_fini(mlxsw_sp);
err_acl_init:
	mlxsw_sp_nve_fini(mlxsw_sp);
err_nve_init:
	mlxsw_sp_afa_fini(mlxsw_sp);
err_afa_init:
	mlxsw_sp_counter_pool_fini(mlxsw_sp);
err_counter_pool_init:
	mlxsw_sp_switchdev_fini(mlxsw_sp);
err_switchdev_init:
	mlxsw_sp_span_fini(mlxsw_sp);
err_span_init:
	mlxsw_sp_lag_fini(mlxsw_sp);
err_lag_init:
	mlxsw_sp_buffers_fini(mlxsw_sp);
err_buffers_init:
	mlxsw_sp_devlink_traps_fini(mlxsw_sp);
err_devlink_traps_init:
	mlxsw_sp_traps_fini(mlxsw_sp);
err_traps_init:
	mlxsw_sp_fids_fini(mlxsw_sp);
err_fids_init:
	mlxsw_sp_kvdl_fini(mlxsw_sp);
	return err;
}

static int mlxsw_sp1_init(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_bus_info *mlxsw_bus_info,
			  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_sp->req_rev = &mlxsw_sp1_fw_rev;
	mlxsw_sp->fw_filename = MLXSW_SP1_FW_FILENAME;
	mlxsw_sp->kvdl_ops = &mlxsw_sp1_kvdl_ops;
	mlxsw_sp->afa_ops = &mlxsw_sp1_act_afa_ops;
	mlxsw_sp->afk_ops = &mlxsw_sp1_afk_ops;
	mlxsw_sp->mr_tcam_ops = &mlxsw_sp1_mr_tcam_ops;
	mlxsw_sp->acl_tcam_ops = &mlxsw_sp1_acl_tcam_ops;
	mlxsw_sp->nve_ops_arr = mlxsw_sp1_nve_ops_arr;
	mlxsw_sp->mac_mask = mlxsw_sp1_mac_mask;
	mlxsw_sp->rif_ops_arr = mlxsw_sp1_rif_ops_arr;
	mlxsw_sp->sb_vals = &mlxsw_sp1_sb_vals;
	mlxsw_sp->port_type_speed_ops = &mlxsw_sp1_port_type_speed_ops;
	mlxsw_sp->ptp_ops = &mlxsw_sp1_ptp_ops;
	mlxsw_sp->span_ops = &mlxsw_sp1_span_ops;
	mlxsw_sp->listeners = mlxsw_sp1_listener;
	mlxsw_sp->listeners_count = ARRAY_SIZE(mlxsw_sp1_listener);
	mlxsw_sp->lowest_shaper_bs = MLXSW_REG_QEEC_LOWEST_SHAPER_BS_SP1;

	return mlxsw_sp_init(mlxsw_core, mlxsw_bus_info, extack);
}

static int mlxsw_sp2_init(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_bus_info *mlxsw_bus_info,
			  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_sp->req_rev = &mlxsw_sp2_fw_rev;
	mlxsw_sp->fw_filename = MLXSW_SP2_FW_FILENAME;
	mlxsw_sp->kvdl_ops = &mlxsw_sp2_kvdl_ops;
	mlxsw_sp->afa_ops = &mlxsw_sp2_act_afa_ops;
	mlxsw_sp->afk_ops = &mlxsw_sp2_afk_ops;
	mlxsw_sp->mr_tcam_ops = &mlxsw_sp2_mr_tcam_ops;
	mlxsw_sp->acl_tcam_ops = &mlxsw_sp2_acl_tcam_ops;
	mlxsw_sp->nve_ops_arr = mlxsw_sp2_nve_ops_arr;
	mlxsw_sp->mac_mask = mlxsw_sp2_mac_mask;
	mlxsw_sp->rif_ops_arr = mlxsw_sp2_rif_ops_arr;
	mlxsw_sp->sb_vals = &mlxsw_sp2_sb_vals;
	mlxsw_sp->port_type_speed_ops = &mlxsw_sp2_port_type_speed_ops;
	mlxsw_sp->ptp_ops = &mlxsw_sp2_ptp_ops;
	mlxsw_sp->span_ops = &mlxsw_sp2_span_ops;
	mlxsw_sp->lowest_shaper_bs = MLXSW_REG_QEEC_LOWEST_SHAPER_BS_SP2;

	return mlxsw_sp_init(mlxsw_core, mlxsw_bus_info, extack);
}

static int mlxsw_sp3_init(struct mlxsw_core *mlxsw_core,
			  const struct mlxsw_bus_info *mlxsw_bus_info,
			  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_sp->kvdl_ops = &mlxsw_sp2_kvdl_ops;
	mlxsw_sp->afa_ops = &mlxsw_sp2_act_afa_ops;
	mlxsw_sp->afk_ops = &mlxsw_sp2_afk_ops;
	mlxsw_sp->mr_tcam_ops = &mlxsw_sp2_mr_tcam_ops;
	mlxsw_sp->acl_tcam_ops = &mlxsw_sp2_acl_tcam_ops;
	mlxsw_sp->nve_ops_arr = mlxsw_sp2_nve_ops_arr;
	mlxsw_sp->mac_mask = mlxsw_sp2_mac_mask;
	mlxsw_sp->rif_ops_arr = mlxsw_sp2_rif_ops_arr;
	mlxsw_sp->sb_vals = &mlxsw_sp2_sb_vals;
	mlxsw_sp->port_type_speed_ops = &mlxsw_sp2_port_type_speed_ops;
	mlxsw_sp->ptp_ops = &mlxsw_sp2_ptp_ops;
	mlxsw_sp->span_ops = &mlxsw_sp3_span_ops;
	mlxsw_sp->lowest_shaper_bs = MLXSW_REG_QEEC_LOWEST_SHAPER_BS_SP3;

	return mlxsw_sp_init(mlxsw_core, mlxsw_bus_info, extack);
}

static void mlxsw_sp_fini(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_sp_ports_remove(mlxsw_sp);
	mlxsw_sp_port_module_info_fini(mlxsw_sp);
	mlxsw_sp_dpipe_fini(mlxsw_sp);
	unregister_netdevice_notifier_net(mlxsw_sp_net(mlxsw_sp),
					  &mlxsw_sp->netdevice_nb);
	if (mlxsw_sp->clock) {
		mlxsw_sp->ptp_ops->fini(mlxsw_sp->ptp_state);
		mlxsw_sp->ptp_ops->clock_fini(mlxsw_sp->clock);
	}
	mlxsw_sp_router_fini(mlxsw_sp);
	mlxsw_sp_acl_fini(mlxsw_sp);
	mlxsw_sp_nve_fini(mlxsw_sp);
	mlxsw_sp_afa_fini(mlxsw_sp);
	mlxsw_sp_counter_pool_fini(mlxsw_sp);
	mlxsw_sp_switchdev_fini(mlxsw_sp);
	mlxsw_sp_span_fini(mlxsw_sp);
	mlxsw_sp_lag_fini(mlxsw_sp);
	mlxsw_sp_buffers_fini(mlxsw_sp);
	mlxsw_sp_devlink_traps_fini(mlxsw_sp);
	mlxsw_sp_traps_fini(mlxsw_sp);
	mlxsw_sp_fids_fini(mlxsw_sp);
	mlxsw_sp_kvdl_fini(mlxsw_sp);
}

/* Per-FID flood tables are used for both "true" 802.1D FIDs and emulated
 * 802.1Q FIDs
 */
#define MLXSW_SP_FID_FLOOD_TABLE_SIZE	(MLXSW_SP_FID_8021D_MAX + \
					 VLAN_VID_MASK - 1)

static const struct mlxsw_config_profile mlxsw_sp1_config_profile = {
	.used_max_mid			= 1,
	.max_mid			= MLXSW_SP_MID_MAX,
	.used_flood_tables		= 1,
	.used_flood_mode		= 1,
	.flood_mode			= 3,
	.max_fid_flood_tables		= 3,
	.fid_flood_table_size		= MLXSW_SP_FID_FLOOD_TABLE_SIZE,
	.used_max_ib_mc			= 1,
	.max_ib_mc			= 0,
	.used_max_pkey			= 1,
	.max_pkey			= 0,
	.used_kvd_sizes			= 1,
	.kvd_hash_single_parts		= 59,
	.kvd_hash_double_parts		= 41,
	.kvd_linear_size		= MLXSW_SP_KVD_LINEAR_SIZE,
	.swid_config			= {
		{
			.used_type	= 1,
			.type		= MLXSW_PORT_SWID_TYPE_ETH,
		}
	},
};

static const struct mlxsw_config_profile mlxsw_sp2_config_profile = {
	.used_max_mid			= 1,
	.max_mid			= MLXSW_SP_MID_MAX,
	.used_flood_tables		= 1,
	.used_flood_mode		= 1,
	.flood_mode			= 3,
	.max_fid_flood_tables		= 3,
	.fid_flood_table_size		= MLXSW_SP_FID_FLOOD_TABLE_SIZE,
	.used_max_ib_mc			= 1,
	.max_ib_mc			= 0,
	.used_max_pkey			= 1,
	.max_pkey			= 0,
	.swid_config			= {
		{
			.used_type	= 1,
			.type		= MLXSW_PORT_SWID_TYPE_ETH,
		}
	},
};

static void
mlxsw_sp_resource_size_params_prepare(struct mlxsw_core *mlxsw_core,
				      struct devlink_resource_size_params *kvd_size_params,
				      struct devlink_resource_size_params *linear_size_params,
				      struct devlink_resource_size_params *hash_double_size_params,
				      struct devlink_resource_size_params *hash_single_size_params)
{
	u32 single_size_min = MLXSW_CORE_RES_GET(mlxsw_core,
						 KVD_SINGLE_MIN_SIZE);
	u32 double_size_min = MLXSW_CORE_RES_GET(mlxsw_core,
						 KVD_DOUBLE_MIN_SIZE);
	u32 kvd_size = MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE);
	u32 linear_size_min = 0;

	devlink_resource_size_params_init(kvd_size_params, kvd_size, kvd_size,
					  MLXSW_SP_KVD_GRANULARITY,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	devlink_resource_size_params_init(linear_size_params, linear_size_min,
					  kvd_size - single_size_min -
					  double_size_min,
					  MLXSW_SP_KVD_GRANULARITY,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	devlink_resource_size_params_init(hash_double_size_params,
					  double_size_min,
					  kvd_size - single_size_min -
					  linear_size_min,
					  MLXSW_SP_KVD_GRANULARITY,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	devlink_resource_size_params_init(hash_single_size_params,
					  single_size_min,
					  kvd_size - double_size_min -
					  linear_size_min,
					  MLXSW_SP_KVD_GRANULARITY,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
}

static int mlxsw_sp1_resources_kvd_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct devlink_resource_size_params hash_single_size_params;
	struct devlink_resource_size_params hash_double_size_params;
	struct devlink_resource_size_params linear_size_params;
	struct devlink_resource_size_params kvd_size_params;
	u32 kvd_size, single_size, double_size, linear_size;
	const struct mlxsw_config_profile *profile;
	int err;

	profile = &mlxsw_sp1_config_profile;
	if (!MLXSW_CORE_RES_VALID(mlxsw_core, KVD_SIZE))
		return -EIO;

	mlxsw_sp_resource_size_params_prepare(mlxsw_core, &kvd_size_params,
					      &linear_size_params,
					      &hash_double_size_params,
					      &hash_single_size_params);

	kvd_size = MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD,
					kvd_size, MLXSW_SP_RESOURCE_KVD,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&kvd_size_params);
	if (err)
		return err;

	linear_size = profile->kvd_linear_size;
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_LINEAR,
					linear_size,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					MLXSW_SP_RESOURCE_KVD,
					&linear_size_params);
	if (err)
		return err;

	err = mlxsw_sp1_kvdl_resources_register(mlxsw_core);
	if  (err)
		return err;

	double_size = kvd_size - linear_size;
	double_size *= profile->kvd_hash_double_parts;
	double_size /= profile->kvd_hash_double_parts +
		       profile->kvd_hash_single_parts;
	double_size = rounddown(double_size, MLXSW_SP_KVD_GRANULARITY);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_HASH_DOUBLE,
					double_size,
					MLXSW_SP_RESOURCE_KVD_HASH_DOUBLE,
					MLXSW_SP_RESOURCE_KVD,
					&hash_double_size_params);
	if (err)
		return err;

	single_size = kvd_size - double_size - linear_size;
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_HASH_SINGLE,
					single_size,
					MLXSW_SP_RESOURCE_KVD_HASH_SINGLE,
					MLXSW_SP_RESOURCE_KVD,
					&hash_single_size_params);
	if (err)
		return err;

	return 0;
}

static int mlxsw_sp2_resources_kvd_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct devlink_resource_size_params kvd_size_params;
	u32 kvd_size;

	if (!MLXSW_CORE_RES_VALID(mlxsw_core, KVD_SIZE))
		return -EIO;

	kvd_size = MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE);
	devlink_resource_size_params_init(&kvd_size_params, kvd_size, kvd_size,
					  MLXSW_SP_KVD_GRANULARITY,
					  DEVLINK_RESOURCE_UNIT_ENTRY);

	return devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD,
					 kvd_size, MLXSW_SP_RESOURCE_KVD,
					 DEVLINK_RESOURCE_ID_PARENT_TOP,
					 &kvd_size_params);
}

static int mlxsw_sp_resources_span_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct devlink_resource_size_params span_size_params;
	u32 max_span;

	if (!MLXSW_CORE_RES_VALID(mlxsw_core, MAX_SPAN))
		return -EIO;

	max_span = MLXSW_CORE_RES_GET(mlxsw_core, MAX_SPAN);
	devlink_resource_size_params_init(&span_size_params, max_span, max_span,
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	return devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_SPAN,
					 max_span, MLXSW_SP_RESOURCE_SPAN,
					 DEVLINK_RESOURCE_ID_PARENT_TOP,
					 &span_size_params);
}

static int mlxsw_sp1_resources_register(struct mlxsw_core *mlxsw_core)
{
	int err;

	err = mlxsw_sp1_resources_kvd_register(mlxsw_core);
	if (err)
		return err;

	err = mlxsw_sp_resources_span_register(mlxsw_core);
	if (err)
		goto err_resources_span_register;

	err = mlxsw_sp_counter_resources_register(mlxsw_core);
	if (err)
		goto err_resources_counter_register;

	return 0;

err_resources_counter_register:
err_resources_span_register:
	devlink_resources_unregister(priv_to_devlink(mlxsw_core), NULL);
	return err;
}

static int mlxsw_sp2_resources_register(struct mlxsw_core *mlxsw_core)
{
	int err;

	err = mlxsw_sp2_resources_kvd_register(mlxsw_core);
	if (err)
		return err;

	err = mlxsw_sp_resources_span_register(mlxsw_core);
	if (err)
		goto err_resources_span_register;

	err = mlxsw_sp_counter_resources_register(mlxsw_core);
	if (err)
		goto err_resources_counter_register;

	return 0;

err_resources_counter_register:
err_resources_span_register:
	devlink_resources_unregister(priv_to_devlink(mlxsw_core), NULL);
	return err;
}

static int mlxsw_sp_kvd_sizes_get(struct mlxsw_core *mlxsw_core,
				  const struct mlxsw_config_profile *profile,
				  u64 *p_single_size, u64 *p_double_size,
				  u64 *p_linear_size)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	u32 double_size;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_core, KVD_SINGLE_MIN_SIZE) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_core, KVD_DOUBLE_MIN_SIZE))
		return -EIO;

	/* The hash part is what left of the kvd without the
	 * linear part. It is split to the single size and
	 * double size by the parts ratio from the profile.
	 * Both sizes must be a multiplications of the
	 * granularity from the profile. In case the user
	 * provided the sizes they are obtained via devlink.
	 */
	err = devlink_resource_size_get(devlink,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					p_linear_size);
	if (err)
		*p_linear_size = profile->kvd_linear_size;

	err = devlink_resource_size_get(devlink,
					MLXSW_SP_RESOURCE_KVD_HASH_DOUBLE,
					p_double_size);
	if (err) {
		double_size = MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE) -
			      *p_linear_size;
		double_size *= profile->kvd_hash_double_parts;
		double_size /= profile->kvd_hash_double_parts +
			       profile->kvd_hash_single_parts;
		*p_double_size = rounddown(double_size,
					   MLXSW_SP_KVD_GRANULARITY);
	}

	err = devlink_resource_size_get(devlink,
					MLXSW_SP_RESOURCE_KVD_HASH_SINGLE,
					p_single_size);
	if (err)
		*p_single_size = MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE) -
				 *p_double_size - *p_linear_size;

	/* Check results are legal. */
	if (*p_single_size < MLXSW_CORE_RES_GET(mlxsw_core, KVD_SINGLE_MIN_SIZE) ||
	    *p_double_size < MLXSW_CORE_RES_GET(mlxsw_core, KVD_DOUBLE_MIN_SIZE) ||
	    MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE) < *p_linear_size)
		return -EIO;

	return 0;
}

static int
mlxsw_sp_devlink_param_fw_load_policy_validate(struct devlink *devlink, u32 id,
					       union devlink_param_value val,
					       struct netlink_ext_ack *extack)
{
	if ((val.vu8 != DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DRIVER) &&
	    (val.vu8 != DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_FLASH)) {
		NL_SET_ERR_MSG_MOD(extack, "'fw_load_policy' must be 'driver' or 'flash'");
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param mlxsw_sp_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(FW_LOAD_POLICY,
			      BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL,
			      mlxsw_sp_devlink_param_fw_load_policy_validate),
};

static int mlxsw_sp_params_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	union devlink_param_value value;
	int err;

	err = devlink_params_register(devlink, mlxsw_sp_devlink_params,
				      ARRAY_SIZE(mlxsw_sp_devlink_params));
	if (err)
		return err;

	value.vu8 = DEVLINK_PARAM_FW_LOAD_POLICY_VALUE_DRIVER;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY,
					   value);
	return 0;
}

static void mlxsw_sp_params_unregister(struct mlxsw_core *mlxsw_core)
{
	devlink_params_unregister(priv_to_devlink(mlxsw_core),
				  mlxsw_sp_devlink_params,
				  ARRAY_SIZE(mlxsw_sp_devlink_params));
}

static int
mlxsw_sp_params_acl_region_rehash_intrvl_get(struct devlink *devlink, u32 id,
					     struct devlink_param_gset_ctx *ctx)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	ctx->val.vu32 = mlxsw_sp_acl_region_rehash_intrvl_get(mlxsw_sp);
	return 0;
}

static int
mlxsw_sp_params_acl_region_rehash_intrvl_set(struct devlink *devlink, u32 id,
					     struct devlink_param_gset_ctx *ctx)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	return mlxsw_sp_acl_region_rehash_intrvl_set(mlxsw_sp, ctx->val.vu32);
}

static const struct devlink_param mlxsw_sp2_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(MLXSW_DEVLINK_PARAM_ID_ACL_REGION_REHASH_INTERVAL,
			     "acl_region_rehash_interval",
			     DEVLINK_PARAM_TYPE_U32,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     mlxsw_sp_params_acl_region_rehash_intrvl_get,
			     mlxsw_sp_params_acl_region_rehash_intrvl_set,
			     NULL),
};

static int mlxsw_sp2_params_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	union devlink_param_value value;
	int err;

	err = mlxsw_sp_params_register(mlxsw_core);
	if (err)
		return err;

	err = devlink_params_register(devlink, mlxsw_sp2_devlink_params,
				      ARRAY_SIZE(mlxsw_sp2_devlink_params));
	if (err)
		goto err_devlink_params_register;

	value.vu32 = 0;
	devlink_param_driverinit_value_set(devlink,
					   MLXSW_DEVLINK_PARAM_ID_ACL_REGION_REHASH_INTERVAL,
					   value);
	return 0;

err_devlink_params_register:
	mlxsw_sp_params_unregister(mlxsw_core);
	return err;
}

static void mlxsw_sp2_params_unregister(struct mlxsw_core *mlxsw_core)
{
	devlink_params_unregister(priv_to_devlink(mlxsw_core),
				  mlxsw_sp2_devlink_params,
				  ARRAY_SIZE(mlxsw_sp2_devlink_params));
	mlxsw_sp_params_unregister(mlxsw_core);
}

static void mlxsw_sp_ptp_transmitted(struct mlxsw_core *mlxsw_core,
				     struct sk_buff *skb, u8 local_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	skb_pull(skb, MLXSW_TXHDR_LEN);
	mlxsw_sp->ptp_ops->transmitted(mlxsw_sp, skb, local_port);
}

static struct mlxsw_driver mlxsw_sp1_driver = {
	.kind				= mlxsw_sp1_driver_name,
	.priv_size			= sizeof(struct mlxsw_sp),
	.init				= mlxsw_sp1_init,
	.fini				= mlxsw_sp_fini,
	.basic_trap_groups_set		= mlxsw_sp_basic_trap_groups_set,
	.port_split			= mlxsw_sp_port_split,
	.port_unsplit			= mlxsw_sp_port_unsplit,
	.sb_pool_get			= mlxsw_sp_sb_pool_get,
	.sb_pool_set			= mlxsw_sp_sb_pool_set,
	.sb_port_pool_get		= mlxsw_sp_sb_port_pool_get,
	.sb_port_pool_set		= mlxsw_sp_sb_port_pool_set,
	.sb_tc_pool_bind_get		= mlxsw_sp_sb_tc_pool_bind_get,
	.sb_tc_pool_bind_set		= mlxsw_sp_sb_tc_pool_bind_set,
	.sb_occ_snapshot		= mlxsw_sp_sb_occ_snapshot,
	.sb_occ_max_clear		= mlxsw_sp_sb_occ_max_clear,
	.sb_occ_port_pool_get		= mlxsw_sp_sb_occ_port_pool_get,
	.sb_occ_tc_port_bind_get	= mlxsw_sp_sb_occ_tc_port_bind_get,
	.flash_update			= mlxsw_sp_flash_update,
	.trap_init			= mlxsw_sp_trap_init,
	.trap_fini			= mlxsw_sp_trap_fini,
	.trap_action_set		= mlxsw_sp_trap_action_set,
	.trap_group_init		= mlxsw_sp_trap_group_init,
	.trap_group_set			= mlxsw_sp_trap_group_set,
	.trap_policer_init		= mlxsw_sp_trap_policer_init,
	.trap_policer_fini		= mlxsw_sp_trap_policer_fini,
	.trap_policer_set		= mlxsw_sp_trap_policer_set,
	.trap_policer_counter_get	= mlxsw_sp_trap_policer_counter_get,
	.txhdr_construct		= mlxsw_sp_txhdr_construct,
	.resources_register		= mlxsw_sp1_resources_register,
	.kvd_sizes_get			= mlxsw_sp_kvd_sizes_get,
	.params_register		= mlxsw_sp_params_register,
	.params_unregister		= mlxsw_sp_params_unregister,
	.ptp_transmitted		= mlxsw_sp_ptp_transmitted,
	.txhdr_len			= MLXSW_TXHDR_LEN,
	.profile			= &mlxsw_sp1_config_profile,
	.res_query_enabled		= true,
};

static struct mlxsw_driver mlxsw_sp2_driver = {
	.kind				= mlxsw_sp2_driver_name,
	.priv_size			= sizeof(struct mlxsw_sp),
	.init				= mlxsw_sp2_init,
	.fini				= mlxsw_sp_fini,
	.basic_trap_groups_set		= mlxsw_sp_basic_trap_groups_set,
	.port_split			= mlxsw_sp_port_split,
	.port_unsplit			= mlxsw_sp_port_unsplit,
	.sb_pool_get			= mlxsw_sp_sb_pool_get,
	.sb_pool_set			= mlxsw_sp_sb_pool_set,
	.sb_port_pool_get		= mlxsw_sp_sb_port_pool_get,
	.sb_port_pool_set		= mlxsw_sp_sb_port_pool_set,
	.sb_tc_pool_bind_get		= mlxsw_sp_sb_tc_pool_bind_get,
	.sb_tc_pool_bind_set		= mlxsw_sp_sb_tc_pool_bind_set,
	.sb_occ_snapshot		= mlxsw_sp_sb_occ_snapshot,
	.sb_occ_max_clear		= mlxsw_sp_sb_occ_max_clear,
	.sb_occ_port_pool_get		= mlxsw_sp_sb_occ_port_pool_get,
	.sb_occ_tc_port_bind_get	= mlxsw_sp_sb_occ_tc_port_bind_get,
	.flash_update			= mlxsw_sp_flash_update,
	.trap_init			= mlxsw_sp_trap_init,
	.trap_fini			= mlxsw_sp_trap_fini,
	.trap_action_set		= mlxsw_sp_trap_action_set,
	.trap_group_init		= mlxsw_sp_trap_group_init,
	.trap_group_set			= mlxsw_sp_trap_group_set,
	.trap_policer_init		= mlxsw_sp_trap_policer_init,
	.trap_policer_fini		= mlxsw_sp_trap_policer_fini,
	.trap_policer_set		= mlxsw_sp_trap_policer_set,
	.trap_policer_counter_get	= mlxsw_sp_trap_policer_counter_get,
	.txhdr_construct		= mlxsw_sp_txhdr_construct,
	.resources_register		= mlxsw_sp2_resources_register,
	.params_register		= mlxsw_sp2_params_register,
	.params_unregister		= mlxsw_sp2_params_unregister,
	.ptp_transmitted		= mlxsw_sp_ptp_transmitted,
	.txhdr_len			= MLXSW_TXHDR_LEN,
	.profile			= &mlxsw_sp2_config_profile,
	.res_query_enabled		= true,
};

static struct mlxsw_driver mlxsw_sp3_driver = {
	.kind				= mlxsw_sp3_driver_name,
	.priv_size			= sizeof(struct mlxsw_sp),
	.init				= mlxsw_sp3_init,
	.fini				= mlxsw_sp_fini,
	.basic_trap_groups_set		= mlxsw_sp_basic_trap_groups_set,
	.port_split			= mlxsw_sp_port_split,
	.port_unsplit			= mlxsw_sp_port_unsplit,
	.sb_pool_get			= mlxsw_sp_sb_pool_get,
	.sb_pool_set			= mlxsw_sp_sb_pool_set,
	.sb_port_pool_get		= mlxsw_sp_sb_port_pool_get,
	.sb_port_pool_set		= mlxsw_sp_sb_port_pool_set,
	.sb_tc_pool_bind_get		= mlxsw_sp_sb_tc_pool_bind_get,
	.sb_tc_pool_bind_set		= mlxsw_sp_sb_tc_pool_bind_set,
	.sb_occ_snapshot		= mlxsw_sp_sb_occ_snapshot,
	.sb_occ_max_clear		= mlxsw_sp_sb_occ_max_clear,
	.sb_occ_port_pool_get		= mlxsw_sp_sb_occ_port_pool_get,
	.sb_occ_tc_port_bind_get	= mlxsw_sp_sb_occ_tc_port_bind_get,
	.flash_update			= mlxsw_sp_flash_update,
	.trap_init			= mlxsw_sp_trap_init,
	.trap_fini			= mlxsw_sp_trap_fini,
	.trap_action_set		= mlxsw_sp_trap_action_set,
	.trap_group_init		= mlxsw_sp_trap_group_init,
	.trap_group_set			= mlxsw_sp_trap_group_set,
	.trap_policer_init		= mlxsw_sp_trap_policer_init,
	.trap_policer_fini		= mlxsw_sp_trap_policer_fini,
	.trap_policer_set		= mlxsw_sp_trap_policer_set,
	.trap_policer_counter_get	= mlxsw_sp_trap_policer_counter_get,
	.txhdr_construct		= mlxsw_sp_txhdr_construct,
	.resources_register		= mlxsw_sp2_resources_register,
	.params_register		= mlxsw_sp2_params_register,
	.params_unregister		= mlxsw_sp2_params_unregister,
	.ptp_transmitted		= mlxsw_sp_ptp_transmitted,
	.txhdr_len			= MLXSW_TXHDR_LEN,
	.profile			= &mlxsw_sp2_config_profile,
	.res_query_enabled		= true,
};

bool mlxsw_sp_port_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &mlxsw_sp_port_netdev_ops;
}

static int mlxsw_sp_lower_dev_walk(struct net_device *lower_dev, void *data)
{
	struct mlxsw_sp_port **p_mlxsw_sp_port = data;
	int ret = 0;

	if (mlxsw_sp_port_dev_check(lower_dev)) {
		*p_mlxsw_sp_port = netdev_priv(lower_dev);
		ret = 1;
	}

	return ret;
}

struct mlxsw_sp_port *mlxsw_sp_port_dev_lower_find(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	if (mlxsw_sp_port_dev_check(dev))
		return netdev_priv(dev);

	mlxsw_sp_port = NULL;
	netdev_walk_all_lower_dev(dev, mlxsw_sp_lower_dev_walk, &mlxsw_sp_port);

	return mlxsw_sp_port;
}

struct mlxsw_sp *mlxsw_sp_lower_get(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find(dev);
	return mlxsw_sp_port ? mlxsw_sp_port->mlxsw_sp : NULL;
}

struct mlxsw_sp_port *mlxsw_sp_port_dev_lower_find_rcu(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	if (mlxsw_sp_port_dev_check(dev))
		return netdev_priv(dev);

	mlxsw_sp_port = NULL;
	netdev_walk_all_lower_dev_rcu(dev, mlxsw_sp_lower_dev_walk,
				      &mlxsw_sp_port);

	return mlxsw_sp_port;
}

struct mlxsw_sp_port *mlxsw_sp_port_lower_dev_hold(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	rcu_read_lock();
	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find_rcu(dev);
	if (mlxsw_sp_port)
		dev_hold(mlxsw_sp_port->dev);
	rcu_read_unlock();
	return mlxsw_sp_port;
}

void mlxsw_sp_port_dev_put(struct mlxsw_sp_port *mlxsw_sp_port)
{
	dev_put(mlxsw_sp_port->dev);
}

static void
mlxsw_sp_port_lag_uppers_cleanup(struct mlxsw_sp_port *mlxsw_sp_port,
				 struct net_device *lag_dev)
{
	struct net_device *br_dev = netdev_master_upper_dev_get(lag_dev);
	struct net_device *upper_dev;
	struct list_head *iter;

	if (netif_is_bridge_port(lag_dev))
		mlxsw_sp_port_bridge_leave(mlxsw_sp_port, lag_dev, br_dev);

	netdev_for_each_upper_dev_rcu(lag_dev, upper_dev, iter) {
		if (!netif_is_bridge_port(upper_dev))
			continue;
		br_dev = netdev_master_upper_dev_get(upper_dev);
		mlxsw_sp_port_bridge_leave(mlxsw_sp_port, upper_dev, br_dev);
	}
}

static int mlxsw_sp_lag_create(struct mlxsw_sp *mlxsw_sp, u16 lag_id)
{
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_create_pack(sldr_pl, lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_destroy(struct mlxsw_sp *mlxsw_sp, u16 lag_id)
{
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_destroy_pack(sldr_pl, lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_col_port_add(struct mlxsw_sp_port *mlxsw_sp_port,
				     u16 lag_id, u8 port_index)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_port_add_pack(slcor_pl, mlxsw_sp_port->local_port,
				      lag_id, port_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_remove(struct mlxsw_sp_port *mlxsw_sp_port,
					u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_port_remove_pack(slcor_pl, mlxsw_sp_port->local_port,
					 lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_enable(struct mlxsw_sp_port *mlxsw_sp_port,
					u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_col_enable_pack(slcor_pl, mlxsw_sp_port->local_port,
					lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_disable(struct mlxsw_sp_port *mlxsw_sp_port,
					 u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_col_disable_pack(slcor_pl, mlxsw_sp_port->local_port,
					 lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_index_get(struct mlxsw_sp *mlxsw_sp,
				  struct net_device *lag_dev,
				  u16 *p_lag_id)
{
	struct mlxsw_sp_upper *lag;
	int free_lag_id = -1;
	u64 max_lag;
	int i;

	max_lag = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_LAG);
	for (i = 0; i < max_lag; i++) {
		lag = mlxsw_sp_lag_get(mlxsw_sp, i);
		if (lag->ref_count) {
			if (lag->dev == lag_dev) {
				*p_lag_id = i;
				return 0;
			}
		} else if (free_lag_id < 0) {
			free_lag_id = i;
		}
	}
	if (free_lag_id < 0)
		return -EBUSY;
	*p_lag_id = free_lag_id;
	return 0;
}

static bool
mlxsw_sp_master_lag_check(struct mlxsw_sp *mlxsw_sp,
			  struct net_device *lag_dev,
			  struct netdev_lag_upper_info *lag_upper_info,
			  struct netlink_ext_ack *extack)
{
	u16 lag_id;

	if (mlxsw_sp_lag_index_get(mlxsw_sp, lag_dev, &lag_id) != 0) {
		NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported LAG devices");
		return false;
	}
	if (lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
		NL_SET_ERR_MSG_MOD(extack, "LAG device using unsupported Tx type");
		return false;
	}
	return true;
}

static int mlxsw_sp_port_lag_index_get(struct mlxsw_sp *mlxsw_sp,
				       u16 lag_id, u8 *p_port_index)
{
	u64 max_lag_members;
	int i;

	max_lag_members = MLXSW_CORE_RES_GET(mlxsw_sp->core,
					     MAX_LAG_MEMBERS);
	for (i = 0; i < max_lag_members; i++) {
		if (!mlxsw_sp_port_lagged_get(mlxsw_sp, lag_id, i)) {
			*p_port_index = i;
			return 0;
		}
	}
	return -EBUSY;
}

static int mlxsw_sp_port_lag_join(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct net_device *lag_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_upper *lag;
	u16 lag_id;
	u8 port_index;
	int err;

	err = mlxsw_sp_lag_index_get(mlxsw_sp, lag_dev, &lag_id);
	if (err)
		return err;
	lag = mlxsw_sp_lag_get(mlxsw_sp, lag_id);
	if (!lag->ref_count) {
		err = mlxsw_sp_lag_create(mlxsw_sp, lag_id);
		if (err)
			return err;
		lag->dev = lag_dev;
	}

	err = mlxsw_sp_port_lag_index_get(mlxsw_sp, lag_id, &port_index);
	if (err)
		return err;
	err = mlxsw_sp_lag_col_port_add(mlxsw_sp_port, lag_id, port_index);
	if (err)
		goto err_col_port_add;

	mlxsw_core_lag_mapping_set(mlxsw_sp->core, lag_id, port_index,
				   mlxsw_sp_port->local_port);
	mlxsw_sp_port->lag_id = lag_id;
	mlxsw_sp_port->lagged = 1;
	lag->ref_count++;

	/* Port is no longer usable as a router interface */
	if (mlxsw_sp_port->default_vlan->fid)
		mlxsw_sp_port_vlan_router_leave(mlxsw_sp_port->default_vlan);

	return 0;

err_col_port_add:
	if (!lag->ref_count)
		mlxsw_sp_lag_destroy(mlxsw_sp, lag_id);
	return err;
}

static void mlxsw_sp_port_lag_leave(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct net_device *lag_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 lag_id = mlxsw_sp_port->lag_id;
	struct mlxsw_sp_upper *lag;

	if (!mlxsw_sp_port->lagged)
		return;
	lag = mlxsw_sp_lag_get(mlxsw_sp, lag_id);
	WARN_ON(lag->ref_count == 0);

	mlxsw_sp_lag_col_port_remove(mlxsw_sp_port, lag_id);

	/* Any VLANs configured on the port are no longer valid */
	mlxsw_sp_port_vlan_flush(mlxsw_sp_port, false);
	mlxsw_sp_port_vlan_cleanup(mlxsw_sp_port->default_vlan);
	/* Make the LAG and its directly linked uppers leave bridges they
	 * are memeber in
	 */
	mlxsw_sp_port_lag_uppers_cleanup(mlxsw_sp_port, lag_dev);

	if (lag->ref_count == 1)
		mlxsw_sp_lag_destroy(mlxsw_sp, lag_id);

	mlxsw_core_lag_mapping_clear(mlxsw_sp->core, lag_id,
				     mlxsw_sp_port->local_port);
	mlxsw_sp_port->lagged = 0;
	lag->ref_count--;

	/* Make sure untagged frames are allowed to ingress */
	mlxsw_sp_port_pvid_set(mlxsw_sp_port, MLXSW_SP_DEFAULT_VID);
}

static int mlxsw_sp_lag_dist_port_add(struct mlxsw_sp_port *mlxsw_sp_port,
				      u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_add_port_pack(sldr_pl, lag_id,
					 mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_dist_port_remove(struct mlxsw_sp_port *mlxsw_sp_port,
					 u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_remove_port_pack(sldr_pl, lag_id,
					    mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int
mlxsw_sp_port_lag_col_dist_enable(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err;

	err = mlxsw_sp_lag_col_port_enable(mlxsw_sp_port,
					   mlxsw_sp_port->lag_id);
	if (err)
		return err;

	err = mlxsw_sp_lag_dist_port_add(mlxsw_sp_port, mlxsw_sp_port->lag_id);
	if (err)
		goto err_dist_port_add;

	return 0;

err_dist_port_add:
	mlxsw_sp_lag_col_port_disable(mlxsw_sp_port, mlxsw_sp_port->lag_id);
	return err;
}

static int
mlxsw_sp_port_lag_col_dist_disable(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err;

	err = mlxsw_sp_lag_dist_port_remove(mlxsw_sp_port,
					    mlxsw_sp_port->lag_id);
	if (err)
		return err;

	err = mlxsw_sp_lag_col_port_disable(mlxsw_sp_port,
					    mlxsw_sp_port->lag_id);
	if (err)
		goto err_col_port_disable;

	return 0;

err_col_port_disable:
	mlxsw_sp_lag_dist_port_add(mlxsw_sp_port, mlxsw_sp_port->lag_id);
	return err;
}

static int mlxsw_sp_port_lag_changed(struct mlxsw_sp_port *mlxsw_sp_port,
				     struct netdev_lag_lower_state_info *info)
{
	if (info->tx_enabled)
		return mlxsw_sp_port_lag_col_dist_enable(mlxsw_sp_port);
	else
		return mlxsw_sp_port_lag_col_dist_disable(mlxsw_sp_port);
}

static int mlxsw_sp_port_stp_set(struct mlxsw_sp_port *mlxsw_sp_port,
				 bool enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	enum mlxsw_reg_spms_state spms_state;
	char *spms_pl;
	u16 vid;
	int err;

	spms_state = enable ? MLXSW_REG_SPMS_STATE_FORWARDING :
			      MLXSW_REG_SPMS_STATE_DISCARDING;

	spms_pl = kmalloc(MLXSW_REG_SPMS_LEN, GFP_KERNEL);
	if (!spms_pl)
		return -ENOMEM;
	mlxsw_reg_spms_pack(spms_pl, mlxsw_sp_port->local_port);

	for (vid = 0; vid < VLAN_N_VID; vid++)
		mlxsw_reg_spms_vid_pack(spms_pl, vid, spms_state);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spms), spms_pl);
	kfree(spms_pl);
	return err;
}

static int mlxsw_sp_port_ovs_join(struct mlxsw_sp_port *mlxsw_sp_port)
{
	u16 vid = 1;
	int err;

	err = mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, true);
	if (err)
		return err;
	err = mlxsw_sp_port_stp_set(mlxsw_sp_port, true);
	if (err)
		goto err_port_stp_set;
	err = mlxsw_sp_port_vlan_set(mlxsw_sp_port, 1, VLAN_N_VID - 2,
				     true, false);
	if (err)
		goto err_port_vlan_set;

	for (; vid <= VLAN_N_VID - 1; vid++) {
		err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_port,
						     vid, false);
		if (err)
			goto err_vid_learning_set;
	}

	return 0;

err_vid_learning_set:
	for (vid--; vid >= 1; vid--)
		mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, true);
err_port_vlan_set:
	mlxsw_sp_port_stp_set(mlxsw_sp_port, false);
err_port_stp_set:
	mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, false);
	return err;
}

static void mlxsw_sp_port_ovs_leave(struct mlxsw_sp_port *mlxsw_sp_port)
{
	u16 vid;

	for (vid = VLAN_N_VID - 1; vid >= 1; vid--)
		mlxsw_sp_port_vid_learning_set(mlxsw_sp_port,
					       vid, true);

	mlxsw_sp_port_vlan_set(mlxsw_sp_port, 1, VLAN_N_VID - 2,
			       false, false);
	mlxsw_sp_port_stp_set(mlxsw_sp_port, false);
	mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, false);
}

static bool mlxsw_sp_bridge_has_multiple_vxlans(struct net_device *br_dev)
{
	unsigned int num_vxlans = 0;
	struct net_device *dev;
	struct list_head *iter;

	netdev_for_each_lower_dev(br_dev, dev, iter) {
		if (netif_is_vxlan(dev))
			num_vxlans++;
	}

	return num_vxlans > 1;
}

static bool mlxsw_sp_bridge_vxlan_vlan_is_valid(struct net_device *br_dev)
{
	DECLARE_BITMAP(vlans, VLAN_N_VID) = {0};
	struct net_device *dev;
	struct list_head *iter;

	netdev_for_each_lower_dev(br_dev, dev, iter) {
		u16 pvid;
		int err;

		if (!netif_is_vxlan(dev))
			continue;

		err = mlxsw_sp_vxlan_mapped_vid(dev, &pvid);
		if (err || !pvid)
			continue;

		if (test_and_set_bit(pvid, vlans))
			return false;
	}

	return true;
}

static bool mlxsw_sp_bridge_vxlan_is_valid(struct net_device *br_dev,
					   struct netlink_ext_ack *extack)
{
	if (br_multicast_enabled(br_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Multicast can not be enabled on a bridge with a VxLAN device");
		return false;
	}

	if (!br_vlan_enabled(br_dev) &&
	    mlxsw_sp_bridge_has_multiple_vxlans(br_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Multiple VxLAN devices are not supported in a VLAN-unaware bridge");
		return false;
	}

	if (br_vlan_enabled(br_dev) &&
	    !mlxsw_sp_bridge_vxlan_vlan_is_valid(br_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Multiple VxLAN devices cannot have the same VLAN as PVID and egress untagged");
		return false;
	}

	return true;
}

static int mlxsw_sp_netdevice_port_upper_event(struct net_device *lower_dev,
					       struct net_device *dev,
					       unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct netlink_ext_ack *extack;
	struct net_device *upper_dev;
	struct mlxsw_sp *mlxsw_sp;
	int err = 0;

	mlxsw_sp_port = netdev_priv(dev);
	mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	info = ptr;
	extack = netdev_notifier_info_to_extack(&info->info);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!is_vlan_dev(upper_dev) &&
		    !netif_is_lag_master(upper_dev) &&
		    !netif_is_bridge_master(upper_dev) &&
		    !netif_is_ovs_master(upper_dev) &&
		    !netif_is_macvlan(upper_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Unknown upper device type");
			return -EINVAL;
		}
		if (!info->linking)
			break;
		if (netif_is_bridge_master(upper_dev) &&
		    !mlxsw_sp_bridge_device_is_offloaded(mlxsw_sp, upper_dev) &&
		    mlxsw_sp_bridge_has_vxlan(upper_dev) &&
		    !mlxsw_sp_bridge_vxlan_is_valid(upper_dev, extack))
			return -EOPNOTSUPP;
		if (netdev_has_any_upper_dev(upper_dev) &&
		    (!netif_is_bridge_master(upper_dev) ||
		     !mlxsw_sp_bridge_device_is_offloaded(mlxsw_sp,
							  upper_dev))) {
			NL_SET_ERR_MSG_MOD(extack, "Enslaving a port to a device that already has an upper device is not supported");
			return -EINVAL;
		}
		if (netif_is_lag_master(upper_dev) &&
		    !mlxsw_sp_master_lag_check(mlxsw_sp, upper_dev,
					       info->upper_info, extack))
			return -EINVAL;
		if (netif_is_lag_master(upper_dev) && vlan_uses_dev(dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Master device is a LAG master and this device has a VLAN");
			return -EINVAL;
		}
		if (netif_is_lag_port(dev) && is_vlan_dev(upper_dev) &&
		    !netif_is_lag_master(vlan_dev_real_dev(upper_dev))) {
			NL_SET_ERR_MSG_MOD(extack, "Can not put a VLAN on a LAG port");
			return -EINVAL;
		}
		if (netif_is_macvlan(upper_dev) &&
		    !mlxsw_sp_rif_exists(mlxsw_sp, lower_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "macvlan is only supported on top of router interfaces");
			return -EOPNOTSUPP;
		}
		if (netif_is_ovs_master(upper_dev) && vlan_uses_dev(dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Master device is an OVS master and this device has a VLAN");
			return -EINVAL;
		}
		if (netif_is_ovs_port(dev) && is_vlan_dev(upper_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Can not put a VLAN on an OVS port");
			return -EINVAL;
		}
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (netif_is_bridge_master(upper_dev)) {
			if (info->linking)
				err = mlxsw_sp_port_bridge_join(mlxsw_sp_port,
								lower_dev,
								upper_dev,
								extack);
			else
				mlxsw_sp_port_bridge_leave(mlxsw_sp_port,
							   lower_dev,
							   upper_dev);
		} else if (netif_is_lag_master(upper_dev)) {
			if (info->linking) {
				err = mlxsw_sp_port_lag_join(mlxsw_sp_port,
							     upper_dev);
			} else {
				mlxsw_sp_port_lag_col_dist_disable(mlxsw_sp_port);
				mlxsw_sp_port_lag_leave(mlxsw_sp_port,
							upper_dev);
			}
		} else if (netif_is_ovs_master(upper_dev)) {
			if (info->linking)
				err = mlxsw_sp_port_ovs_join(mlxsw_sp_port);
			else
				mlxsw_sp_port_ovs_leave(mlxsw_sp_port);
		} else if (netif_is_macvlan(upper_dev)) {
			if (!info->linking)
				mlxsw_sp_rif_macvlan_del(mlxsw_sp, upper_dev);
		} else if (is_vlan_dev(upper_dev)) {
			struct net_device *br_dev;

			if (!netif_is_bridge_port(upper_dev))
				break;
			if (info->linking)
				break;
			br_dev = netdev_master_upper_dev_get(upper_dev);
			mlxsw_sp_port_bridge_leave(mlxsw_sp_port, upper_dev,
						   br_dev);
		}
		break;
	}

	return err;
}

static int mlxsw_sp_netdevice_port_lower_event(struct net_device *dev,
					       unsigned long event, void *ptr)
{
	struct netdev_notifier_changelowerstate_info *info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	mlxsw_sp_port = netdev_priv(dev);
	info = ptr;

	switch (event) {
	case NETDEV_CHANGELOWERSTATE:
		if (netif_is_lag_port(dev) && mlxsw_sp_port->lagged) {
			err = mlxsw_sp_port_lag_changed(mlxsw_sp_port,
							info->lower_state_info);
			if (err)
				netdev_err(dev, "Failed to reflect link aggregation lower state change\n");
		}
		break;
	}

	return 0;
}

static int mlxsw_sp_netdevice_port_event(struct net_device *lower_dev,
					 struct net_device *port_dev,
					 unsigned long event, void *ptr)
{
	switch (event) {
	case NETDEV_PRECHANGEUPPER:
	case NETDEV_CHANGEUPPER:
		return mlxsw_sp_netdevice_port_upper_event(lower_dev, port_dev,
							   event, ptr);
	case NETDEV_CHANGELOWERSTATE:
		return mlxsw_sp_netdevice_port_lower_event(port_dev, event,
							   ptr);
	}

	return 0;
}

static int mlxsw_sp_netdevice_lag_event(struct net_device *lag_dev,
					unsigned long event, void *ptr)
{
	struct net_device *dev;
	struct list_head *iter;
	int ret;

	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		if (mlxsw_sp_port_dev_check(dev)) {
			ret = mlxsw_sp_netdevice_port_event(lag_dev, dev, event,
							    ptr);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int mlxsw_sp_netdevice_port_vlan_event(struct net_device *vlan_dev,
					      struct net_device *dev,
					      unsigned long event, void *ptr,
					      u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct netdev_notifier_changeupper_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct net_device *upper_dev;
	int err = 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!netif_is_bridge_master(upper_dev) &&
		    !netif_is_macvlan(upper_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Unknown upper device type");
			return -EINVAL;
		}
		if (!info->linking)
			break;
		if (netif_is_bridge_master(upper_dev) &&
		    !mlxsw_sp_bridge_device_is_offloaded(mlxsw_sp, upper_dev) &&
		    mlxsw_sp_bridge_has_vxlan(upper_dev) &&
		    !mlxsw_sp_bridge_vxlan_is_valid(upper_dev, extack))
			return -EOPNOTSUPP;
		if (netdev_has_any_upper_dev(upper_dev) &&
		    (!netif_is_bridge_master(upper_dev) ||
		     !mlxsw_sp_bridge_device_is_offloaded(mlxsw_sp,
							  upper_dev))) {
			NL_SET_ERR_MSG_MOD(extack, "Enslaving a port to a device that already has an upper device is not supported");
			return -EINVAL;
		}
		if (netif_is_macvlan(upper_dev) &&
		    !mlxsw_sp_rif_exists(mlxsw_sp, vlan_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "macvlan is only supported on top of router interfaces");
			return -EOPNOTSUPP;
		}
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (netif_is_bridge_master(upper_dev)) {
			if (info->linking)
				err = mlxsw_sp_port_bridge_join(mlxsw_sp_port,
								vlan_dev,
								upper_dev,
								extack);
			else
				mlxsw_sp_port_bridge_leave(mlxsw_sp_port,
							   vlan_dev,
							   upper_dev);
		} else if (netif_is_macvlan(upper_dev)) {
			if (!info->linking)
				mlxsw_sp_rif_macvlan_del(mlxsw_sp, upper_dev);
		} else {
			err = -EINVAL;
			WARN_ON(1);
		}
		break;
	}

	return err;
}

static int mlxsw_sp_netdevice_lag_port_vlan_event(struct net_device *vlan_dev,
						  struct net_device *lag_dev,
						  unsigned long event,
						  void *ptr, u16 vid)
{
	struct net_device *dev;
	struct list_head *iter;
	int ret;

	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		if (mlxsw_sp_port_dev_check(dev)) {
			ret = mlxsw_sp_netdevice_port_vlan_event(vlan_dev, dev,
								 event, ptr,
								 vid);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int mlxsw_sp_netdevice_bridge_vlan_event(struct net_device *vlan_dev,
						struct net_device *br_dev,
						unsigned long event, void *ptr,
						u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(vlan_dev);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct net_device *upper_dev;

	if (!mlxsw_sp)
		return 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!netif_is_macvlan(upper_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Unknown upper device type");
			return -EOPNOTSUPP;
		}
		if (!info->linking)
			break;
		if (netif_is_macvlan(upper_dev) &&
		    !mlxsw_sp_rif_exists(mlxsw_sp, vlan_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "macvlan is only supported on top of router interfaces");
			return -EOPNOTSUPP;
		}
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (info->linking)
			break;
		if (netif_is_macvlan(upper_dev))
			mlxsw_sp_rif_macvlan_del(mlxsw_sp, upper_dev);
		break;
	}

	return 0;
}

static int mlxsw_sp_netdevice_vlan_event(struct net_device *vlan_dev,
					 unsigned long event, void *ptr)
{
	struct net_device *real_dev = vlan_dev_real_dev(vlan_dev);
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	if (mlxsw_sp_port_dev_check(real_dev))
		return mlxsw_sp_netdevice_port_vlan_event(vlan_dev, real_dev,
							  event, ptr, vid);
	else if (netif_is_lag_master(real_dev))
		return mlxsw_sp_netdevice_lag_port_vlan_event(vlan_dev,
							      real_dev, event,
							      ptr, vid);
	else if (netif_is_bridge_master(real_dev))
		return mlxsw_sp_netdevice_bridge_vlan_event(vlan_dev, real_dev,
							    event, ptr, vid);

	return 0;
}

static int mlxsw_sp_netdevice_bridge_event(struct net_device *br_dev,
					   unsigned long event, void *ptr)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(br_dev);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct net_device *upper_dev;

	if (!mlxsw_sp)
		return 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!is_vlan_dev(upper_dev) && !netif_is_macvlan(upper_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Unknown upper device type");
			return -EOPNOTSUPP;
		}
		if (!info->linking)
			break;
		if (netif_is_macvlan(upper_dev) &&
		    !mlxsw_sp_rif_exists(mlxsw_sp, br_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "macvlan is only supported on top of router interfaces");
			return -EOPNOTSUPP;
		}
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (info->linking)
			break;
		if (is_vlan_dev(upper_dev))
			mlxsw_sp_rif_destroy_by_dev(mlxsw_sp, upper_dev);
		if (netif_is_macvlan(upper_dev))
			mlxsw_sp_rif_macvlan_del(mlxsw_sp, upper_dev);
		break;
	}

	return 0;
}

static int mlxsw_sp_netdevice_macvlan_event(struct net_device *macvlan_dev,
					    unsigned long event, void *ptr)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(macvlan_dev);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct netlink_ext_ack *extack;

	if (!mlxsw_sp || event != NETDEV_PRECHANGEUPPER)
		return 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	/* VRF enslavement is handled in mlxsw_sp_netdevice_vrf_event() */
	NL_SET_ERR_MSG_MOD(extack, "Unknown upper device type");

	return -EOPNOTSUPP;
}

static bool mlxsw_sp_is_vrf_event(unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info = ptr;

	if (event != NETDEV_PRECHANGEUPPER && event != NETDEV_CHANGEUPPER)
		return false;
	return netif_is_l3_master(info->upper_dev);
}

static int mlxsw_sp_netdevice_vxlan_event(struct mlxsw_sp *mlxsw_sp,
					  struct net_device *dev,
					  unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *cu_info;
	struct netdev_notifier_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct net_device *upper_dev;

	extack = netdev_notifier_info_to_extack(info);

	switch (event) {
	case NETDEV_CHANGEUPPER:
		cu_info = container_of(info,
				       struct netdev_notifier_changeupper_info,
				       info);
		upper_dev = cu_info->upper_dev;
		if (!netif_is_bridge_master(upper_dev))
			return 0;
		if (!mlxsw_sp_lower_get(upper_dev))
			return 0;
		if (!mlxsw_sp_bridge_vxlan_is_valid(upper_dev, extack))
			return -EOPNOTSUPP;
		if (cu_info->linking) {
			if (!netif_running(dev))
				return 0;
			/* When the bridge is VLAN-aware, the VNI of the VxLAN
			 * device needs to be mapped to a VLAN, but at this
			 * point no VLANs are configured on the VxLAN device
			 */
			if (br_vlan_enabled(upper_dev))
				return 0;
			return mlxsw_sp_bridge_vxlan_join(mlxsw_sp, upper_dev,
							  dev, 0, extack);
		} else {
			/* VLANs were already flushed, which triggered the
			 * necessary cleanup
			 */
			if (br_vlan_enabled(upper_dev))
				return 0;
			mlxsw_sp_bridge_vxlan_leave(mlxsw_sp, dev);
		}
		break;
	case NETDEV_PRE_UP:
		upper_dev = netdev_master_upper_dev_get(dev);
		if (!upper_dev)
			return 0;
		if (!netif_is_bridge_master(upper_dev))
			return 0;
		if (!mlxsw_sp_lower_get(upper_dev))
			return 0;
		return mlxsw_sp_bridge_vxlan_join(mlxsw_sp, upper_dev, dev, 0,
						  extack);
	case NETDEV_DOWN:
		upper_dev = netdev_master_upper_dev_get(dev);
		if (!upper_dev)
			return 0;
		if (!netif_is_bridge_master(upper_dev))
			return 0;
		if (!mlxsw_sp_lower_get(upper_dev))
			return 0;
		mlxsw_sp_bridge_vxlan_leave(mlxsw_sp, dev);
		break;
	}

	return 0;
}

static int mlxsw_sp_netdevice_event(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct mlxsw_sp_span_entry *span_entry;
	struct mlxsw_sp *mlxsw_sp;
	int err = 0;

	mlxsw_sp = container_of(nb, struct mlxsw_sp, netdevice_nb);
	if (event == NETDEV_UNREGISTER) {
		span_entry = mlxsw_sp_span_entry_find_by_port(mlxsw_sp, dev);
		if (span_entry)
			mlxsw_sp_span_entry_invalidate(mlxsw_sp, span_entry);
	}
	mlxsw_sp_span_respin(mlxsw_sp);

	if (netif_is_vxlan(dev))
		err = mlxsw_sp_netdevice_vxlan_event(mlxsw_sp, dev, event, ptr);
	if (mlxsw_sp_netdev_is_ipip_ol(mlxsw_sp, dev))
		err = mlxsw_sp_netdevice_ipip_ol_event(mlxsw_sp, dev,
						       event, ptr);
	else if (mlxsw_sp_netdev_is_ipip_ul(mlxsw_sp, dev))
		err = mlxsw_sp_netdevice_ipip_ul_event(mlxsw_sp, dev,
						       event, ptr);
	else if (event == NETDEV_PRE_CHANGEADDR ||
		 event == NETDEV_CHANGEADDR ||
		 event == NETDEV_CHANGEMTU)
		err = mlxsw_sp_netdevice_router_port_event(dev, event, ptr);
	else if (mlxsw_sp_is_vrf_event(event, ptr))
		err = mlxsw_sp_netdevice_vrf_event(dev, event, ptr);
	else if (mlxsw_sp_port_dev_check(dev))
		err = mlxsw_sp_netdevice_port_event(dev, dev, event, ptr);
	else if (netif_is_lag_master(dev))
		err = mlxsw_sp_netdevice_lag_event(dev, event, ptr);
	else if (is_vlan_dev(dev))
		err = mlxsw_sp_netdevice_vlan_event(dev, event, ptr);
	else if (netif_is_bridge_master(dev))
		err = mlxsw_sp_netdevice_bridge_event(dev, event, ptr);
	else if (netif_is_macvlan(dev))
		err = mlxsw_sp_netdevice_macvlan_event(dev, event, ptr);

	return notifier_from_errno(err);
}

static struct notifier_block mlxsw_sp_inetaddr_valid_nb __read_mostly = {
	.notifier_call = mlxsw_sp_inetaddr_valid_event,
};

static struct notifier_block mlxsw_sp_inet6addr_valid_nb __read_mostly = {
	.notifier_call = mlxsw_sp_inet6addr_valid_event,
};

static const struct pci_device_id mlxsw_sp1_pci_id_table[] = {
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SPECTRUM), 0},
	{0, },
};

static struct pci_driver mlxsw_sp1_pci_driver = {
	.name = mlxsw_sp1_driver_name,
	.id_table = mlxsw_sp1_pci_id_table,
};

static const struct pci_device_id mlxsw_sp2_pci_id_table[] = {
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SPECTRUM2), 0},
	{0, },
};

static struct pci_driver mlxsw_sp2_pci_driver = {
	.name = mlxsw_sp2_driver_name,
	.id_table = mlxsw_sp2_pci_id_table,
};

static const struct pci_device_id mlxsw_sp3_pci_id_table[] = {
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SPECTRUM3), 0},
	{0, },
};

static struct pci_driver mlxsw_sp3_pci_driver = {
	.name = mlxsw_sp3_driver_name,
	.id_table = mlxsw_sp3_pci_id_table,
};

static int __init mlxsw_sp_module_init(void)
{
	int err;

	register_inetaddr_validator_notifier(&mlxsw_sp_inetaddr_valid_nb);
	register_inet6addr_validator_notifier(&mlxsw_sp_inet6addr_valid_nb);

	err = mlxsw_core_driver_register(&mlxsw_sp1_driver);
	if (err)
		goto err_sp1_core_driver_register;

	err = mlxsw_core_driver_register(&mlxsw_sp2_driver);
	if (err)
		goto err_sp2_core_driver_register;

	err = mlxsw_core_driver_register(&mlxsw_sp3_driver);
	if (err)
		goto err_sp3_core_driver_register;

	err = mlxsw_pci_driver_register(&mlxsw_sp1_pci_driver);
	if (err)
		goto err_sp1_pci_driver_register;

	err = mlxsw_pci_driver_register(&mlxsw_sp2_pci_driver);
	if (err)
		goto err_sp2_pci_driver_register;

	err = mlxsw_pci_driver_register(&mlxsw_sp3_pci_driver);
	if (err)
		goto err_sp3_pci_driver_register;

	return 0;

err_sp3_pci_driver_register:
	mlxsw_pci_driver_unregister(&mlxsw_sp2_pci_driver);
err_sp2_pci_driver_register:
	mlxsw_pci_driver_unregister(&mlxsw_sp1_pci_driver);
err_sp1_pci_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_sp3_driver);
err_sp3_core_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_sp2_driver);
err_sp2_core_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_sp1_driver);
err_sp1_core_driver_register:
	unregister_inet6addr_validator_notifier(&mlxsw_sp_inet6addr_valid_nb);
	unregister_inetaddr_validator_notifier(&mlxsw_sp_inetaddr_valid_nb);
	return err;
}

static void __exit mlxsw_sp_module_exit(void)
{
	mlxsw_pci_driver_unregister(&mlxsw_sp3_pci_driver);
	mlxsw_pci_driver_unregister(&mlxsw_sp2_pci_driver);
	mlxsw_pci_driver_unregister(&mlxsw_sp1_pci_driver);
	mlxsw_core_driver_unregister(&mlxsw_sp3_driver);
	mlxsw_core_driver_unregister(&mlxsw_sp2_driver);
	mlxsw_core_driver_unregister(&mlxsw_sp1_driver);
	unregister_inet6addr_validator_notifier(&mlxsw_sp_inet6addr_valid_nb);
	unregister_inetaddr_validator_notifier(&mlxsw_sp_inetaddr_valid_nb);
}

module_init(mlxsw_sp_module_init);
module_exit(mlxsw_sp_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox Spectrum driver");
MODULE_DEVICE_TABLE(pci, mlxsw_sp1_pci_id_table);
MODULE_DEVICE_TABLE(pci, mlxsw_sp2_pci_id_table);
MODULE_DEVICE_TABLE(pci, mlxsw_sp3_pci_id_table);
MODULE_FIRMWARE(MLXSW_SP1_FW_FILENAME);
MODULE_FIRMWARE(MLXSW_SP2_FW_FILENAME);
