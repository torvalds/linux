
#include <linux/types.h>

/* -------------------------------------------------------------------------- */

/* Command Status and Connector Change Indication (CCI) data structure */
struct ucsi_cci {
	unsigned int RESERVED1:1;
	unsigned int connector_change:7;
	u8 data_length;
	unsigned int RESERVED9:9;
	unsigned int not_supported:1;
	unsigned int cancel_complete:1;
	unsigned int reset_complete:1;
	unsigned int busy:1;
	unsigned int ack_complete:1;
	unsigned int error:1;
	unsigned int cmd_complete:1;
} __packed;

/* Default fields in CONTROL data structure */
struct ucsi_command {
	u8 cmd;
	u8 length;
	u64 data:48;
} __packed;

/* Set USB Operation Mode Command structure */
struct ucsi_uor_cmd {
	u8 cmd;
	u8 length;
	u64 con_num:7;
	u64 role:3;
#define UCSI_UOR_ROLE_DFP			BIT(0)
#define UCSI_UOR_ROLE_UFP			BIT(1)
#define UCSI_UOR_ROLE_DRP			BIT(2)
	u64 data:38;
} __packed;

struct ucsi_control {
	union {
		u64 raw_cmd;
		struct ucsi_command cmd;
		struct ucsi_uor_cmd uor;
	};
};

struct ucsi_data {
	u16 version;
	u16 RESERVED;
	union {
		u32 raw_cci;
		struct ucsi_cci cci;
	};
	struct ucsi_control ctrl;
	u32 message_in[4];
	u32 message_out[4];
} __packed;

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
#define UCSI_SET_PDM			0x0A
#define UCSI_SET_PDR			0x0B
#define UCSI_GET_ALTERNATE_MODES	0x0C
#define UCSI_GET_CAM_SUPPORTED		0x0D
#define UCSI_GET_CURRENT_CAM		0x0E
#define UCSI_SET_NEW_CAM		0x0F
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
	u8 num_connectors;
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
	u8 RESERVED;
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
	u8 RESERVED_2:2;
	u8 latency:4;
	u8 RESERVED_4:4;
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
#define UCSI_CONSTAT_PWR_OPMODE_TYPEC1_3	4
#define UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0	5
	u16 connected:1;
	u16 pwr_dir:1;
	u16 partner_flags:8;
#define UCSI_CONSTAT_PARTNER_FLAG_USB		BIT(0)
#define UCSI_CONSTAT_PARTNER_FLAG_ALT_MODE	BIT(1)
	u16 partner_type:3;
#define UCSI_CONSTAT_PARTNER_TYPE_DFP		1
#define UCSI_CONSTAT_PARTNER_TYPE_UFP		2
#define UCSI_CONSTAT_PARTNER_TYPE_CABLE_NO_UFP	3 /* Powered Cable */
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
	u8 RESERVED:2;
} __packed;

/* -------------------------------------------------------------------------- */

