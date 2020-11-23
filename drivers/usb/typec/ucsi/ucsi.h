/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __DRIVER_USB_TYPEC_UCSI_H
#define __DRIVER_USB_TYPEC_UCSI_H

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/types.h>
#include <linux/usb/typec.h>

/* -------------------------------------------------------------------------- */

struct ucsi;
struct ucsi_altmode;

/* UCSI offsets (Bytes) */
#define UCSI_VERSION			0
#define UCSI_CCI			4
#define UCSI_CONTROL			8
#define UCSI_MESSAGE_IN			16
#define UCSI_MESSAGE_OUT		32

/* Command Status and Connector Change Indication (CCI) bits */
#define UCSI_CCI_CONNECTOR(_c_)		(((_c_) & GENMASK(7, 1)) >> 1)
#define UCSI_CCI_LENGTH(_c_)		(((_c_) & GENMASK(15, 8)) >> 8)
#define UCSI_CCI_NOT_SUPPORTED		BIT(25)
#define UCSI_CCI_CANCEL_COMPLETE	BIT(26)
#define UCSI_CCI_RESET_COMPLETE		BIT(27)
#define UCSI_CCI_BUSY			BIT(28)
#define UCSI_CCI_ACK_COMPLETE		BIT(29)
#define UCSI_CCI_ERROR			BIT(30)
#define UCSI_CCI_COMMAND_COMPLETE	BIT(31)

/**
 * struct ucsi_operations - UCSI I/O operations
 * @read: Read operation
 * @sync_write: Blocking write operation
 * @async_write: Non-blocking write operation
 * @update_altmodes: Squashes duplicate DP altmodes
 *
 * Read and write routines for UCSI interface. @sync_write must wait for the
 * Command Completion Event from the PPM before returning, and @async_write must
 * return immediately after sending the data to the PPM.
 */
struct ucsi_operations {
	int (*read)(struct ucsi *ucsi, unsigned int offset,
		    void *val, size_t val_len);
	int (*sync_write)(struct ucsi *ucsi, unsigned int offset,
			  const void *val, size_t val_len);
	int (*async_write)(struct ucsi *ucsi, unsigned int offset,
			   const void *val, size_t val_len);
	bool (*update_altmodes)(struct ucsi *ucsi, struct ucsi_altmode *orig,
				struct ucsi_altmode *updated);
};

struct ucsi *ucsi_create(struct device *dev, const struct ucsi_operations *ops);
void ucsi_destroy(struct ucsi *ucsi);
int ucsi_register(struct ucsi *ucsi);
void ucsi_unregister(struct ucsi *ucsi);
void *ucsi_get_drvdata(struct ucsi *ucsi);
void ucsi_set_drvdata(struct ucsi *ucsi, void *data);

void ucsi_connector_change(struct ucsi *ucsi, u8 num);

/* -------------------------------------------------------------------------- */

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

#define UCSI_CONNECTOR_NUMBER(_num_)		((u64)(_num_) << 16)
#define UCSI_COMMAND(_cmd_)			((_cmd_) & 0xff)

/* CONNECTOR_RESET command bits */
#define UCSI_CONNECTOR_RESET_HARD		BIT(23) /* Deprecated in v1.1 */

/* ACK_CC_CI bits */
#define UCSI_ACK_CONNECTOR_CHANGE		BIT(16)
#define UCSI_ACK_COMMAND_COMPLETE		BIT(17)

/* SET_NOTIFICATION_ENABLE command bits */
#define UCSI_ENABLE_NTFY_CMD_COMPLETE		BIT(16)
#define UCSI_ENABLE_NTFY_EXT_PWR_SRC_CHANGE	BIT(17)
#define UCSI_ENABLE_NTFY_PWR_OPMODE_CHANGE	BIT(18)
#define UCSI_ENABLE_NTFY_CAP_CHANGE		BIT(21)
#define UCSI_ENABLE_NTFY_PWR_LEVEL_CHANGE	BIT(22)
#define UCSI_ENABLE_NTFY_PD_RESET_COMPLETE	BIT(23)
#define UCSI_ENABLE_NTFY_CAM_CHANGE		BIT(24)
#define UCSI_ENABLE_NTFY_BAT_STATUS_CHANGE	BIT(25)
#define UCSI_ENABLE_NTFY_PARTNER_CHANGE		BIT(27)
#define UCSI_ENABLE_NTFY_PWR_DIR_CHANGE		BIT(28)
#define UCSI_ENABLE_NTFY_CONNECTOR_CHANGE	BIT(30)
#define UCSI_ENABLE_NTFY_ERROR			BIT(31)
#define UCSI_ENABLE_NTFY_ALL			0xdbe70000

/* SET_UOR command bits */
#define UCSI_SET_UOR_ROLE(_r_)		(((_r_) == TYPEC_HOST ? 1 : 2) << 23)
#define UCSI_SET_UOR_ACCEPT_ROLE_SWAPS		BIT(25)

/* SET_PDF command bits */
#define UCSI_SET_PDR_ROLE(_r_)		(((_r_) == TYPEC_SOURCE ? 1 : 2) << 23)
#define UCSI_SET_PDR_ACCEPT_ROLE_SWAPS		BIT(25)

/* GET_ALTERNATE_MODES command bits */
#define UCSI_ALTMODE_RECIPIENT(_r_)		(((_r_) >> 16) & 0x7)
#define UCSI_GET_ALTMODE_RECIPIENT(_r_)		((u64)(_r_) << 16)
#define   UCSI_RECIPIENT_CON			0
#define   UCSI_RECIPIENT_SOP			1
#define   UCSI_RECIPIENT_SOP_P			2
#define   UCSI_RECIPIENT_SOP_PP			3
#define UCSI_GET_ALTMODE_CONNECTOR_NUMBER(_r_)	((u64)(_r_) << 24)
#define UCSI_ALTMODE_OFFSET(_r_)		(((_r_) >> 32) & 0xff)
#define UCSI_GET_ALTMODE_OFFSET(_r_)		((u64)(_r_) << 32)
#define UCSI_GET_ALTMODE_NUM_ALTMODES(_r_)	((u64)(_r_) << 40)

/* GET_PDOS command bits */
#define UCSI_GET_PDOS_PARTNER_PDO(_r_)		((u64)(_r_) << 23)
#define UCSI_GET_PDOS_NUM_PDOS(_r_)		((u64)(_r_) << 32)
#define UCSI_GET_PDOS_SRC_PDOS			((u64)1 << 34)

/* -------------------------------------------------------------------------- */

/* Error information returned by PPM in response to GET_ERROR_STATUS command. */
#define UCSI_ERROR_UNREGONIZED_CMD		BIT(0)
#define UCSI_ERROR_INVALID_CON_NUM		BIT(1)
#define UCSI_ERROR_INVALID_CMD_ARGUMENT		BIT(2)
#define UCSI_ERROR_INCOMPATIBLE_PARTNER		BIT(3)
#define UCSI_ERROR_CC_COMMUNICATION_ERR		BIT(4)
#define UCSI_ERROR_DEAD_BATTERY			BIT(5)
#define UCSI_ERROR_CONTRACT_NEGOTIATION_FAIL	BIT(6)
#define UCSI_ERROR_OVERCURRENT			BIT(7)
#define UCSI_ERROR_UNDEFINED			BIT(8)
#define UCSI_ERROR_PARTNER_REJECTED_SWAP	BIT(9)
#define UCSI_ERROR_HARD_RESET			BIT(10)
#define UCSI_ERROR_PPM_POLICY_CONFLICT		BIT(11)
#define UCSI_ERROR_SWAP_REJECTED		BIT(12)

#define UCSI_SET_NEW_CAM_ENTER(x)		(((x) >> 23) & 0x1)
#define UCSI_SET_NEW_CAM_GET_AM(x)		(((x) >> 24) & 0xff)
#define UCSI_SET_NEW_CAM_AM_MASK		(0xff << 24)
#define UCSI_SET_NEW_CAM_SET_AM(x)		(((x) & 0xff) << 24)
#define UCSI_CMD_CONNECTOR_MASK			(0x7)

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
	u8 features;
#define UCSI_CAP_SET_UOM			BIT(0)
#define UCSI_CAP_SET_PDM			BIT(1)
#define UCSI_CAP_ALT_MODE_DETAILS		BIT(2)
#define UCSI_CAP_ALT_MODE_OVERRIDE		BIT(3)
#define UCSI_CAP_PDO_DETAILS			BIT(4)
#define UCSI_CAP_CABLE_DETAILS			BIT(5)
#define UCSI_CAP_EXT_SUPPLY_NOTIFICATIONS	BIT(6)
#define UCSI_CAP_PD_RESET			BIT(7)
	u16 reserved_1;
	u8 num_alt_modes;
	u8 reserved_2;
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
	u8 flags;
#define UCSI_CONCAP_FLAG_PROVIDER		BIT(0)
#define UCSI_CONCAP_FLAG_CONSUMER		BIT(1)
} __packed;

struct ucsi_altmode {
	u16 svid;
	u32 mid;
} __packed;

/* Data structure filled by PPM in response to GET_CABLE_PROPERTY command. */
struct ucsi_cable_property {
	u16 speed_supported;
	u8 current_capability;
	u8 flags;
#define UCSI_CABLE_PROP_FLAG_VBUS_IN_CABLE	BIT(0)
#define UCSI_CABLE_PROP_FLAG_ACTIVE_CABLE	BIT(1)
#define UCSI_CABLE_PROP_FLAG_DIRECTIONALITY	BIT(2)
#define UCSI_CABLE_PROP_FLAG_PLUG_TYPE(_f_)	((_f_) & GENMASK(3, 0))
#define   UCSI_CABLE_PROPERTY_PLUG_TYPE_A	0
#define   UCSI_CABLE_PROPERTY_PLUG_TYPE_B	1
#define   UCSI_CABLE_PROPERTY_PLUG_TYPE_C	2
#define   UCSI_CABLE_PROPERTY_PLUG_OTHER	3
#define UCSI_CABLE_PROP_MODE_SUPPORT		BIT(5)
	u8 latency;
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
	u16 flags;
#define UCSI_CONSTAT_PWR_OPMODE(_f_)		((_f_) & GENMASK(2, 0))
#define   UCSI_CONSTAT_PWR_OPMODE_NONE		0
#define   UCSI_CONSTAT_PWR_OPMODE_DEFAULT	1
#define   UCSI_CONSTAT_PWR_OPMODE_BC		2
#define   UCSI_CONSTAT_PWR_OPMODE_PD		3
#define   UCSI_CONSTAT_PWR_OPMODE_TYPEC1_5	4
#define   UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0	5
#define UCSI_CONSTAT_CONNECTED			BIT(3)
#define UCSI_CONSTAT_PWR_DIR			BIT(4)
#define UCSI_CONSTAT_PARTNER_FLAGS(_f_)		(((_f_) & GENMASK(12, 5)) >> 5)
#define   UCSI_CONSTAT_PARTNER_FLAG_USB		1
#define   UCSI_CONSTAT_PARTNER_FLAG_ALT_MODE	2
#define UCSI_CONSTAT_PARTNER_TYPE(_f_)		(((_f_) & GENMASK(15, 13)) >> 13)
#define   UCSI_CONSTAT_PARTNER_TYPE_DFP		1
#define   UCSI_CONSTAT_PARTNER_TYPE_UFP		2
#define   UCSI_CONSTAT_PARTNER_TYPE_CABLE	3 /* Powered Cable */
#define   UCSI_CONSTAT_PARTNER_TYPE_CABLE_AND_UFP	4 /* Powered Cable */
#define   UCSI_CONSTAT_PARTNER_TYPE_DEBUG	5
#define   UCSI_CONSTAT_PARTNER_TYPE_AUDIO	6
	u32 request_data_obj;
	u8 pwr_status;
#define UCSI_CONSTAT_BC_STATUS(_p_)		((_p_) & GENMASK(2, 0))
#define   UCSI_CONSTAT_BC_NOT_CHARGING		0
#define   UCSI_CONSTAT_BC_NOMINAL_CHARGING	1
#define   UCSI_CONSTAT_BC_SLOW_CHARGING		2
#define   UCSI_CONSTAT_BC_TRICKLE_CHARGING	3
#define UCSI_CONSTAT_PROVIDER_CAP_LIMIT(_p_)	(((_p_) & GENMASK(6, 3)) >> 3)
#define   UCSI_CONSTAT_CAP_PWR_LOWERED		0
#define   UCSI_CONSTAT_CAP_PWR_BUDGET_LIMIT	1
} __packed;

/* -------------------------------------------------------------------------- */

struct ucsi {
	u16 version;
	struct device *dev;
	struct driver_data *driver_data;

	const struct ucsi_operations *ops;

	struct ucsi_capability cap;
	struct ucsi_connector *connector;

	struct work_struct work;

	/* PPM Communication lock */
	struct mutex ppm_lock;

	/* The latest "Notification Enable" bits (SET_NOTIFICATION_ENABLE) */
	u64 ntfy;

	/* PPM communication flags */
	unsigned long flags;
#define EVENT_PENDING	0
#define COMMAND_PENDING	1
#define ACK_PENDING	2
};

#define UCSI_MAX_SVID		5
#define UCSI_MAX_ALTMODES	(UCSI_MAX_SVID * 6)
#define UCSI_MAX_PDOS		(4)

#define UCSI_TYPEC_VSAFE5V	5000
#define UCSI_TYPEC_1_5_CURRENT	1500
#define UCSI_TYPEC_3_0_CURRENT	3000

struct ucsi_connector {
	int num;

	struct ucsi *ucsi;
	struct mutex lock; /* port lock */
	struct work_struct work;
	struct completion complete;

	struct typec_port *port;
	struct typec_partner *partner;

	struct typec_altmode *port_altmode[UCSI_MAX_ALTMODES];
	struct typec_altmode *partner_altmode[UCSI_MAX_ALTMODES];

	struct typec_capability typec_cap;

	struct ucsi_connector_status status;
	struct ucsi_connector_capability cap;
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	u32 rdo;
	u32 src_pdos[UCSI_MAX_PDOS];
	int num_pdos;
};

int ucsi_send_command(struct ucsi *ucsi, u64 command,
		      void *retval, size_t size);

void ucsi_altmode_update_active(struct ucsi_connector *con);
int ucsi_resume(struct ucsi *ucsi);

#if IS_ENABLED(CONFIG_POWER_SUPPLY)
int ucsi_register_port_psy(struct ucsi_connector *con);
void ucsi_unregister_port_psy(struct ucsi_connector *con);
void ucsi_port_psy_changed(struct ucsi_connector *con);
#else
static inline int ucsi_register_port_psy(struct ucsi_connector *con) { return 0; }
static inline void ucsi_unregister_port_psy(struct ucsi_connector *con) { }
static inline void ucsi_port_psy_changed(struct ucsi_connector *con) { }
#endif /* CONFIG_POWER_SUPPLY */

#if IS_ENABLED(CONFIG_TYPEC_DP_ALTMODE)
struct typec_altmode *
ucsi_register_displayport(struct ucsi_connector *con,
			  bool override, int offset,
			  struct typec_altmode_desc *desc);

void ucsi_displayport_remove_partner(struct typec_altmode *adev);

#else
static inline struct typec_altmode *
ucsi_register_displayport(struct ucsi_connector *con,
			  bool override, int offset,
			  struct typec_altmode_desc *desc)
{
	return NULL;
}

static inline void
ucsi_displayport_remove_partner(struct typec_altmode *adev) { }
#endif /* CONFIG_TYPEC_DP_ALTMODE */

/*
 * NVIDIA VirtualLink (svid 0x955) has two altmode. VirtualLink
 * DP mode with vdo=0x1 and NVIDIA test mode with vdo=0x3
 */
#define USB_TYPEC_NVIDIA_VLINK_DP_VDO	0x1
#define USB_TYPEC_NVIDIA_VLINK_DBG_VDO	0x3

#endif /* __DRIVER_USB_TYPEC_UCSI_H */
