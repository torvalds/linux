
#ifndef __DRIVER_USB_TYPEC_UCSI_H
#define __DRIVER_USB_TYPEC_UCSI_H

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/types.h>

/* -------------------------------------------------------------------------- */

/* Command Status and Connector Change Indication (CCI) data structure */
struct ucsi_cci {
	u8:1; /* reserved */
	u8 connector_change:7;
	u8 data_length;
	u16:9; /* reserved */
	u16 not_supported:1;
	u16 cancel_complete:1;
	u16 reset_complete:1;
	u16 busy:1;
	u16 ack_complete:1;
	u16 error:1;
	u16 cmd_complete:1;
} __packed;

/* Default fields in CONTROL data structure */
struct ucsi_command {
	u8 cmd;
	u8 length;
	u64 data:48;
} __packed;

/* ACK Command structure */
struct ucsi_ack_cmd {
	u8 cmd;
	u8 length;
	u8 cci_ack:1;
	u8 cmd_ack:1;
	u8:6; /* reserved */
} __packed;

/* Connector Reset Command structure */
struct ucsi_con_rst {
	u8 cmd;
	u8 length;
	u8 con_num:7;
	u8 hard_reset:1;
} __packed;

/* Set USB Operation Mode Command structure */
struct ucsi_uor_cmd {
	u8 cmd;
	u8 length;
	u16 con_num:7;
	u16 role:3;
#define UCSI_UOR_ROLE_DFP			BIT(0)
#define UCSI_UOR_ROLE_UFP			BIT(1)
#define UCSI_UOR_ROLE_DRP			BIT(2)
	u16:6; /* reserved */
} __packed;

struct ucsi_control {
	union {
		u64 raw_cmd;
		struct ucsi_command cmd;
		struct ucsi_uor_cmd uor;
		struct ucsi_ack_cmd ack;
		struct ucsi_con_rst con_rst;
	};
};

#define __UCSI_CMD(_ctrl_, _cmd_)					\
{									\
	(_ctrl_).raw_cmd = 0;						\
	(_ctrl_).cmd.cmd = _cmd_;					\
}

/* Helper for preparing ucsi_control for CONNECTOR_RESET command. */
#define UCSI_CMD_CONNECTOR_RESET(_ctrl_, _con_, _hard_)			\
{									\
	__UCSI_CMD(_ctrl_, UCSI_CONNECTOR_RESET)			\
	(_ctrl_).con_rst.con_num = (_con_)->num;			\
	(_ctrl_).con_rst.hard_reset = _hard_;				\
}

/* Helper for preparing ucsi_control for ACK_CC_CI command. */
#define UCSI_CMD_ACK(_ctrl_, _ack_)					\
{									\
	__UCSI_CMD(_ctrl_, UCSI_ACK_CC_CI)				\
	(_ctrl_).ack.cci_ack = ((_ack_) == UCSI_ACK_EVENT);		\
	(_ctrl_).ack.cmd_ack = ((_ack_) == UCSI_ACK_CMD);		\
}

/* Helper for preparing ucsi_control for SET_NOTIFY_ENABLE command. */
#define UCSI_CMD_SET_NTFY_ENABLE(_ctrl_, _ntfys_)			\
{									\
	__UCSI_CMD(_ctrl_, UCSI_SET_NOTIFICATION_ENABLE)		\
	(_ctrl_).cmd.data = _ntfys_;					\
}

/* Helper for preparing ucsi_control for GET_CAPABILITY command. */
#define UCSI_CMD_GET_CAPABILITY(_ctrl_)					\
{									\
	__UCSI_CMD(_ctrl_, UCSI_GET_CAPABILITY)				\
}

/* Helper for preparing ucsi_control for GET_CONNECTOR_CAPABILITY command. */
#define UCSI_CMD_GET_CONNECTOR_CAPABILITY(_ctrl_, _con_)		\
{									\
	__UCSI_CMD(_ctrl_, UCSI_GET_CONNECTOR_CAPABILITY)		\
	(_ctrl_).cmd.data = _con_;					\
}

/* Helper for preparing ucsi_control for GET_CONNECTOR_STATUS command. */
#define UCSI_CMD_GET_CONNECTOR_STATUS(_ctrl_, _con_)			\
{									\
	__UCSI_CMD(_ctrl_, UCSI_GET_CONNECTOR_STATUS)			\
	(_ctrl_).cmd.data = _con_;					\
}

#define __UCSI_ROLE(_ctrl_, _cmd_, _con_num_)				\
{									\
	__UCSI_CMD(_ctrl_, _cmd_)					\
	(_ctrl_).uor.con_num = _con_num_;				\
	(_ctrl_).uor.role = UCSI_UOR_ROLE_DRP;				\
}

/* Helper for preparing ucsi_control for SET_UOR command. */
#define UCSI_CMD_SET_UOR(_ctrl_, _con_, _role_)				\
{									\
	__UCSI_ROLE(_ctrl_, UCSI_SET_UOR, (_con_)->num)		\
	(_ctrl_).uor.role |= (_role_) == TYPEC_HOST ? UCSI_UOR_ROLE_DFP : \
			  UCSI_UOR_ROLE_UFP;				\
}

/* Helper for preparing ucsi_control for SET_PDR command. */
#define UCSI_CMD_SET_PDR(_ctrl_, _con_, _role_)			\
{									\
	__UCSI_ROLE(_ctrl_, UCSI_SET_PDR, (_con_)->num)		\
	(_ctrl_).uor.role |= (_role_) == TYPEC_SOURCE ? UCSI_UOR_ROLE_DFP : \
			UCSI_UOR_ROLE_UFP;				\
}

/* Commands */
#define UCSI_PPM_RESET			0x01
#define UCSI_CANCEL			0x02
#define UCSI_CONNECTOR_RESET		0x03
#define UCSI_ACK_CC_CI			0x04
#define UCSI_SET_NOTIFICATION_ENABLE	0x05
#define UCSI_GET_CAPABILITY		0x06
#define UCSI_GET_CONNECTOR_CAPABILITY	0x07
#define UCSI_SET_UOM			0x08
#define UCSI_SET_UOR			0x09
#define UCSI_SET_PDM			0x0a
#define UCSI_SET_PDR			0x0b
#define UCSI_GET_ALTERNATE_MODES	0x0c
#define UCSI_GET_CAM_SUPPORTED		0x0d
#define UCSI_GET_CURRENT_CAM		0x0e
#define UCSI_SET_NEW_CAM		0x0f
#define UCSI_GET_PDOS			0x10
#define UCSI_GET_CABLE_PROPERTY		0x11
#define UCSI_GET_CONNECTOR_STATUS	0x12
#define UCSI_GET_ERROR_STATUS		0x13

/* ACK_CC_CI commands */
#define UCSI_ACK_EVENT			1
#define UCSI_ACK_CMD			2

/* Bits for SET_NOTIFICATION_ENABLE command */
#define UCSI_ENABLE_NTFY_CMD_COMPLETE		BIT(0)
#define UCSI_ENABLE_NTFY_EXT_PWR_SRC_CHANGE	BIT(1)
#define UCSI_ENABLE_NTFY_PWR_OPMODE_CHANGE	BIT(2)
#define UCSI_ENABLE_NTFY_CAP_CHANGE		BIT(5)
#define UCSI_ENABLE_NTFY_PWR_LEVEL_CHANGE	BIT(6)
#define UCSI_ENABLE_NTFY_PD_RESET_COMPLETE	BIT(7)
#define UCSI_ENABLE_NTFY_CAM_CHANGE		BIT(8)
#define UCSI_ENABLE_NTFY_BAT_STATUS_CHANGE	BIT(9)
#define UCSI_ENABLE_NTFY_PARTNER_CHANGE		BIT(11)
#define UCSI_ENABLE_NTFY_PWR_DIR_CHANGE		BIT(12)
#define UCSI_ENABLE_NTFY_CONNECTOR_CHANGE	BIT(14)
#define UCSI_ENABLE_NTFY_ERROR			BIT(15)
#define UCSI_ENABLE_NTFY_ALL			0xdbe7

/* Error information returned by PPM in response to GET_ERROR_STATUS command. */
#define UCSI_ERROR_UNREGONIZED_CMD		BIT(0)
#define UCSI_ERROR_INVALID_CON_NUM		BIT(1)
#define UCSI_ERROR_INVALID_CMD_ARGUMENT		BIT(2)
#define UCSI_ERROR_INCOMPATIBLE_PARTNER		BIT(3)
#define UCSI_ERROR_CC_COMMUNICATION_ERR		BIT(4)
#define UCSI_ERROR_DEAD_BATTERY			BIT(5)
#define UCSI_ERROR_CONTRACT_NEGOTIATION_FAIL	BIT(6)

/* Data structure filled by PPM in response to GET_CAPABILITY command. */
struct ucsi_capability {
	u32 attributes;
#define UCSI_CAP_ATTR_DISABLE_STATE		BIT(0)
#define UCSI_CAP_ATTR_BATTERY_CHARGING		BIT(1)
#define UCSI_CAP_ATTR_USB_PD			BIT(2)
#define UCSI_CAP_ATTR_TYPEC_CURRENT		BIT(6)
#define UCSI_CAP_ATTR_POWER_AC_SUPPLY		BIT(8)
#define UCSI_CAP_ATTR_POWER_OTHER		BIT(10)
#define UCSI_CAP_ATTR_POWER_VBUS		BIT(14)
	u32 num_connectors:8;
	u32 features:24;
#define UCSI_CAP_SET_UOM			BIT(0)
#define UCSI_CAP_SET_PDM			BIT(1)
#define UCSI_CAP_ALT_MODE_DETAILS		BIT(2)
#define UCSI_CAP_ALT_MODE_OVERRIDE		BIT(3)
#define UCSI_CAP_PDO_DETAILS			BIT(4)
#define UCSI_CAP_CABLE_DETAILS			BIT(5)
#define UCSI_CAP_EXT_SUPPLY_NOTIFICATIONS	BIT(6)
#define UCSI_CAP_PD_RESET			BIT(7)
	u8 num_alt_modes;
	u8 reserved;
	u16 bc_version;
	u16 pd_version;
	u16 typec_version;
} __packed;

/* Data structure filled by PPM in response to GET_CONNECTOR_CAPABILITY cmd. */
struct ucsi_connector_capability {
	u8 op_mode;
#define UCSI_CONCAP_OPMODE_DFP			BIT(0)
#define UCSI_CONCAP_OPMODE_UFP			BIT(1)
#define UCSI_CONCAP_OPMODE_DRP			BIT(2)
#define UCSI_CONCAP_OPMODE_AUDIO_ACCESSORY	BIT(3)
#define UCSI_CONCAP_OPMODE_DEBUG_ACCESSORY	BIT(4)
#define UCSI_CONCAP_OPMODE_USB2			BIT(5)
#define UCSI_CONCAP_OPMODE_USB3			BIT(6)
#define UCSI_CONCAP_OPMODE_ALT_MODE		BIT(7)
	u8 provider:1;
	u8 consumer:1;
	u8:6; /* reserved */
} __packed;

struct ucsi_altmode {
	u16 svid;
	u32 mid;
} __packed;

/* Data structure filled by PPM in response to GET_CABLE_PROPERTY command. */
struct ucsi_cable_property {
	u16 speed_supported;
	u8 current_capability;
	u8 vbus_in_cable:1;
	u8 active_cable:1;
	u8 directionality:1;
	u8 plug_type:2;
#define UCSI_CABLE_PROPERTY_PLUG_TYPE_A		0
#define UCSI_CABLE_PROPERTY_PLUG_TYPE_B		1
#define UCSI_CABLE_PROPERTY_PLUG_TYPE_C		2
#define UCSI_CABLE_PROPERTY_PLUG_OTHER		3
	u8 mode_support:1;
	u8:2; /* reserved */
	u8 latency:4;
	u8:4; /* reserved */
} __packed;

/* Data structure filled by PPM in response to GET_CONNECTOR_STATUS command. */
struct ucsi_connector_status {
	u16 change;
#define UCSI_CONSTAT_EXT_SUPPLY_CHANGE		BIT(1)
#define UCSI_CONSTAT_POWER_OPMODE_CHANGE	BIT(2)
#define UCSI_CONSTAT_PDOS_CHANGE		BIT(5)
#define UCSI_CONSTAT_POWER_LEVEL_CHANGE		BIT(6)
#define UCSI_CONSTAT_PD_RESET_COMPLETE		BIT(7)
#define UCSI_CONSTAT_CAM_CHANGE			BIT(8)
#define UCSI_CONSTAT_BC_CHANGE			BIT(9)
#define UCSI_CONSTAT_PARTNER_CHANGE		BIT(11)
#define UCSI_CONSTAT_POWER_DIR_CHANGE		BIT(12)
#define UCSI_CONSTAT_CONNECT_CHANGE		BIT(14)
#define UCSI_CONSTAT_ERROR			BIT(15)
	u16 pwr_op_mode:3;
#define UCSI_CONSTAT_PWR_OPMODE_NONE		0
#define UCSI_CONSTAT_PWR_OPMODE_DEFAULT		1
#define UCSI_CONSTAT_PWR_OPMODE_BC		2
#define UCSI_CONSTAT_PWR_OPMODE_PD		3
#define UCSI_CONSTAT_PWR_OPMODE_TYPEC1_5	4
#define UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0	5
	u16 connected:1;
	u16 pwr_dir:1;
	u16 partner_flags:8;
#define UCSI_CONSTAT_PARTNER_FLAG_USB		BIT(0)
#define UCSI_CONSTAT_PARTNER_FLAG_ALT_MODE	BIT(1)
	u16 partner_type:3;
#define UCSI_CONSTAT_PARTNER_TYPE_DFP		1
#define UCSI_CONSTAT_PARTNER_TYPE_UFP		2
#define UCSI_CONSTAT_PARTNER_TYPE_CABLE		3 /* Powered Cable */
#define UCSI_CONSTAT_PARTNER_TYPE_CABLE_AND_UFP	4 /* Powered Cable */
#define UCSI_CONSTAT_PARTNER_TYPE_DEBUG		5
#define UCSI_CONSTAT_PARTNER_TYPE_AUDIO		6
	u32 request_data_obj;
	u8 bc_status:2;
#define UCSI_CONSTAT_BC_NOT_CHARGING		0
#define UCSI_CONSTAT_BC_NOMINAL_CHARGING	1
#define UCSI_CONSTAT_BC_SLOW_CHARGING		2
#define UCSI_CONSTAT_BC_TRICKLE_CHARGING	3
	u8 provider_cap_limit_reason:4;
#define UCSI_CONSTAT_CAP_PWR_LOWERED		0
#define UCSI_CONSTAT_CAP_PWR_BUDGET_LIMIT	1
	u8:2; /* reserved */
} __packed;

/* -------------------------------------------------------------------------- */

struct ucsi;

struct ucsi_data {
	u16 version;
	u16 reserved;
	union {
		u32 raw_cci;
		struct ucsi_cci cci;
	};
	struct ucsi_control ctrl;
	u32 message_in[4];
	u32 message_out[4];
} __packed;

/*
 * struct ucsi_ppm - Interface to UCSI Platform Policy Manager
 * @data: memory location to the UCSI data structures
 * @cmd: UCSI command execution routine
 * @sync: Refresh UCSI mailbox (the data structures)
 */
struct ucsi_ppm {
	struct ucsi_data *data;
	int (*cmd)(struct ucsi_ppm *, struct ucsi_control *);
	int (*sync)(struct ucsi_ppm *);
};

struct ucsi *ucsi_register_ppm(struct device *dev, struct ucsi_ppm *ppm);
void ucsi_unregister_ppm(struct ucsi *ucsi);
void ucsi_notify(struct ucsi *ucsi);

#endif /* __DRIVER_USB_TYPEC_UCSI_H */
