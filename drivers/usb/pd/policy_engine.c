// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/ipc_logging.h>
#include <linux/iio/consumer.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/power_supply.h>
#include <linux/qti_power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/extcon-provider.h>
#include <linux/usb/typec.h>
#include <linux/usb/usbpd.h>

#include "usbpd.h"

enum usbpd_state {
	PE_UNKNOWN,
	PE_ERROR_RECOVERY,
	PE_SRC_DISABLED,
	PE_SRC_STARTUP,
	PE_SRC_STARTUP_WAIT_FOR_VDM_RESP,
	PE_SRC_SEND_CAPABILITIES,
	PE_SRC_SEND_CAPABILITIES_WAIT, /* substate to wait for Request */
	PE_SRC_NEGOTIATE_CAPABILITY,
	PE_SRC_TRANSITION_SUPPLY,
	PE_SRC_READY,
	PE_SRC_HARD_RESET,
	PE_SRC_SOFT_RESET,
	PE_SRC_DISCOVERY,
	PE_SRC_TRANSITION_TO_DEFAULT,
	PE_SNK_STARTUP,
	PE_SNK_DISCOVERY,
	PE_SNK_WAIT_FOR_CAPABILITIES,
	PE_SNK_EVALUATE_CAPABILITY,
	PE_SNK_SELECT_CAPABILITY,
	PE_SNK_TRANSITION_SINK,
	PE_SNK_READY,
	PE_SNK_HARD_RESET,
	PE_SNK_SOFT_RESET,
	PE_SNK_TRANSITION_TO_DEFAULT,
	PE_DRS_SEND_DR_SWAP,
	PE_PRS_SNK_SRC_SEND_SWAP,
	PE_PRS_SNK_SRC_TRANSITION_TO_OFF,
	PE_PRS_SNK_SRC_SOURCE_ON,
	PE_PRS_SRC_SNK_SEND_SWAP,
	PE_PRS_SRC_SNK_TRANSITION_TO_OFF,
	PE_PRS_SRC_SNK_WAIT_SOURCE_ON,
	PE_SEND_SOFT_RESET,
	PE_VCS_WAIT_FOR_VCONN,
	PE_MAX_STATES,
};

static const char * const usbpd_state_strings[] = {
	"UNKNOWN",
	"ERROR_RECOVERY",
	"SRC_Disabled",
	"SRC_Startup",
	"SRC_Startup_Wait_for_VDM_Resp",
	"SRC_Send_Capabilities",
	"SRC_Send_Capabilities (Wait for Request)",
	"SRC_Negotiate_Capability",
	"SRC_Transition_Supply",
	"SRC_Ready",
	"SRC_Hard_Reset",
	"SRC_Soft_Reset",
	"SRC_Discovery",
	"SRC_Transition_to_default",
	"SNK_Startup",
	"SNK_Discovery",
	"SNK_Wait_for_Capabilities",
	"SNK_Evaluate_Capability",
	"SNK_Select_Capability",
	"SNK_Transition_Sink",
	"SNK_Ready",
	"SNK_Hard_Reset",
	"SNK_Soft_Reset",
	"SNK_Transition_to_default",
	"DRS_Send_DR_Swap",
	"PRS_SNK_SRC_Send_Swap",
	"PRS_SNK_SRC_Transition_to_off",
	"PRS_SNK_SRC_Source_on",
	"PRS_SRC_SNK_Send_Swap",
	"PRS_SRC_SNK_Transition_to_off",
	"PRS_SRC_SNK_Wait_Source_on",
	"Send_Soft_Reset",
	"VCS_Wait_for_VCONN",
};

enum usbpd_control_msg_type {
	MSG_RESERVED = 0,
	MSG_GOODCRC,
	MSG_GOTOMIN,
	MSG_ACCEPT,
	MSG_REJECT,
	MSG_PING,
	MSG_PS_RDY,
	MSG_GET_SOURCE_CAP,
	MSG_GET_SINK_CAP,
	MSG_DR_SWAP,
	MSG_PR_SWAP,
	MSG_VCONN_SWAP,
	MSG_WAIT,
	MSG_SOFT_RESET,
	MSG_NOT_SUPPORTED = 0x10,
	MSG_GET_SOURCE_CAP_EXTENDED,
	MSG_GET_STATUS,
	MSG_FR_SWAP,
	MSG_GET_PPS_STATUS,
	MSG_GET_COUNTRY_CODES,
};

static const char * const usbpd_control_msg_strings[] = {
	"", "GoodCRC", "GotoMin", "Accept", "Reject", "Ping", "PS_RDY",
	"Get_Source_Cap", "Get_Sink_Cap", "DR_Swap", "PR_Swap", "VCONN_Swap",
	"Wait", "Soft_Reset", "", "", "Not_Supported",
	"Get_Source_Cap_Extended", "Get_Status", "FR_Swap", "Get_PPS_Status",
	"Get_Country_Codes",
};

enum usbpd_data_msg_type {
	MSG_SOURCE_CAPABILITIES = 1,
	MSG_REQUEST,
	MSG_BIST,
	MSG_SINK_CAPABILITIES,
	MSG_BATTERY_STATUS,
	MSG_ALERT,
	MSG_GET_COUNTRY_INFO,
	MSG_VDM = 0xF,
};

static const char * const usbpd_data_msg_strings[] = {
	"", "Source_Capabilities", "Request", "BIST", "Sink_Capabilities",
	"Battery_Status", "Alert", "Get_Country_Info", "", "", "", "", "", "",
	"", "Vendor_Defined",
};

enum usbpd_ext_msg_type {
	MSG_SOURCE_CAPABILITIES_EXTENDED = 1,
	MSG_STATUS,
	MSG_GET_BATTERY_CAP,
	MSG_GET_BATTERY_STATUS,
	MSG_BATTERY_CAPABILITIES,
	MSG_GET_MANUFACTURER_INFO,
	MSG_MANUFACTURER_INFO,
	MSG_SECURITY_REQUEST,
	MSG_SECURITY_RESPONSE,
	MSG_FIRMWARE_UPDATE_REQUEST,
	MSG_FIRMWARE_UPDATE_RESPONSE,
	MSG_PPS_STATUS,
	MSG_COUNTRY_INFO,
	MSG_COUNTRY_CODES,
};

static const char * const usbpd_ext_msg_strings[] = {
	"", "Source_Capabilities_Extended", "Status", "Get_Battery_Cap",
	"Get_Battery_Status", "Get_Manufacturer_Info", "Manufacturer_Info",
	"Security_Request", "Security_Response", "Firmware_Update_Request",
	"Firmware_Update_Response", "PPS_Status", "Country_Info",
	"Country_Codes",
};

enum iio_psy_property {
	POWER_SUPPLY_PROP_PD_ACTIVE = 0,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
	POWER_SUPPLY_PROP_CONNECTOR_TYPE,
	POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
	POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED,
	POWER_SUPPLY_PROP_TYPEC_SRC_RP,
	POWER_SUPPLY_PROP_PD_IN_HARD_RESET,
	POWER_SUPPLY_PROP_PD_CURRENT_MAX,
	POWER_SUPPLY_PROP_PR_SWAP,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_TYPEC_MODE,
	POWER_SUPPLY_PROP_PE_START,
	POWER_SUPPLY_IIO_PROP_MAX,
};

static const char * const usbpd_iio_channel_map[] = {
	"pd_active", "typec_cc_orientation", "connector_type",
	"typec_power_role", "pd_usb_suspend_supported", "typec_src_rp",
	"pd_in_hard_reset", "pr_current_max", "pr_swap", "pd_voltage_min",
	"pd_voltage_max", "real_type", "typec_mode", "pe_start",
};

static inline const char *msg_to_string(u8 id, bool is_data, bool is_ext)
{
	if (is_ext) {
		if (id < ARRAY_SIZE(usbpd_ext_msg_strings))
			return usbpd_ext_msg_strings[id];
	} else if (is_data) {
		if (id < ARRAY_SIZE(usbpd_data_msg_strings))
			return usbpd_data_msg_strings[id];
	} else if (id < ARRAY_SIZE(usbpd_control_msg_strings)) {
		return usbpd_control_msg_strings[id];
	}

	return "Invalid";
}

enum vdm_state {
	VDM_NONE,
	DISCOVERED_ID,
	DISCOVERED_SVIDS,
	DISCOVERED_MODES,
	MODE_ENTERED,
	MODE_EXITED,
};

static void *usbpd_ipc_log;
#define usbpd_dbg(dev, fmt, ...) do { \
	ipc_log_string(usbpd_ipc_log, "%s: %s: " fmt, dev_name(dev), __func__, \
			##__VA_ARGS__); \
	dev_dbg(dev, fmt, ##__VA_ARGS__); \
	} while (0)

#define usbpd_info(dev, fmt, ...) do { \
	ipc_log_string(usbpd_ipc_log, "%s: %s: " fmt, dev_name(dev), __func__, \
			##__VA_ARGS__); \
	dev_info(dev, fmt, ##__VA_ARGS__); \
	} while (0)

#define usbpd_warn(dev, fmt, ...) do { \
	ipc_log_string(usbpd_ipc_log, "%s: %s: " fmt, dev_name(dev), __func__, \
			##__VA_ARGS__); \
	dev_warn(dev, fmt, ##__VA_ARGS__); \
	} while (0)

#define usbpd_err(dev, fmt, ...) do { \
	ipc_log_string(usbpd_ipc_log, "%s: %s: " fmt, dev_name(dev), __func__, \
			##__VA_ARGS__); \
	dev_err(dev, fmt, ##__VA_ARGS__); \
	} while (0)

#define NUM_LOG_PAGES		10

/* Timeouts (in ms) */
#define ERROR_RECOVERY_TIME	25
#define SENDER_RESPONSE_TIME	26
#define SINK_WAIT_CAP_TIME	500
#define PS_TRANSITION_TIME	450
#define SRC_CAP_TIME		120
#define SRC_TRANSITION_TIME	25
#define SRC_RECOVER_TIME	750
#define PS_HARD_RESET_TIME	25
#define PS_SOURCE_ON		400
#define PS_SOURCE_OFF		750
#define FIRST_SOURCE_CAP_TIME	100
#define VDM_BUSY_TIME		50
#define VCONN_ON_TIME		100
#define SINK_TX_TIME		16

/* tPSHardReset + tSafe0V */
#define SNK_HARD_RESET_VBUS_OFF_TIME	(35 + 650)

/* tSrcRecover + tSrcTurnOn */
#define SNK_HARD_RESET_VBUS_ON_TIME	(1000 + 275)

#define PD_CAPS_COUNT		50

#define PD_MAX_MSG_ID		7

#define PD_MAX_DATA_OBJ		7

#define PD_SRC_CAP_EXT_DB_LEN	24
#define PD_STATUS_DB_LEN	6
#define PD_BATTERY_CAP_DB_LEN	9

#define PD_MAX_EXT_MSG_LEN		260
#define PD_MAX_EXT_MSG_LEGACY_LEN	26

#define PD_MSG_HDR(type, dr, pr, id, cnt, rev) \
	(((type) & 0x1F) | ((dr) << 5) | (rev << 6) | \
	 ((pr) << 8) | ((id) << 9) | ((cnt) << 12))
#define PD_MSG_HDR_COUNT(hdr)		(((hdr) >> 12) & 7)
#define PD_MSG_HDR_TYPE(hdr)		((hdr) & 0x1F)
#define PD_MSG_HDR_ID(hdr)		(((hdr) >> 9) & 7)
#define PD_MSG_HDR_REV(hdr)		(((hdr) >> 6) & 3)
#define PD_MSG_HDR_EXTENDED		BIT(15)
#define PD_MSG_HDR_IS_EXTENDED(hdr)	((hdr) & PD_MSG_HDR_EXTENDED)

#define PD_MSG_EXT_HDR(chunked, num, req, size) \
	(((chunked) << 15) | (((num) & 0xF) << 11) | \
	 ((req) << 10) | ((size) & 0x1FF))
#define PD_MSG_EXT_HDR_IS_CHUNKED(ehdr)	((ehdr) & 0x8000)
#define PD_MSG_EXT_HDR_CHUNK_NUM(ehdr)	(((ehdr) >> 11) & 0xF)
#define PD_MSG_EXT_HDR_REQ_CHUNK(ehdr)	((ehdr) & 0x400)
#define PD_MSG_EXT_HDR_DATA_SIZE(ehdr)	((ehdr) & 0x1FF)

#define PD_RDO_FIXED(obj, gb, mismatch, usb_comm, no_usb_susp, curr1, curr2) \
		(((obj) << 28) | ((gb) << 27) | ((mismatch) << 26) | \
		 ((usb_comm) << 25) | ((no_usb_susp) << 24) | \
		 ((curr1) << 10) | (curr2))

#define PD_RDO_AUGMENTED(obj, mismatch, usb_comm, no_usb_susp, volt, curr) \
		(((obj) << 28) | ((mismatch) << 26) | ((usb_comm) << 25) | \
		 ((no_usb_susp) << 24) | ((volt) << 9) | (curr))

#define PD_RDO_OBJ_POS(rdo)		((rdo) >> 28 & 7)
#define PD_RDO_GIVEBACK(rdo)		((rdo) >> 27 & 1)
#define PD_RDO_MISMATCH(rdo)		((rdo) >> 26 & 1)
#define PD_RDO_USB_COMM(rdo)		((rdo) >> 25 & 1)
#define PD_RDO_NO_USB_SUSP(rdo)		((rdo) >> 24 & 1)
#define PD_RDO_FIXED_CURR(rdo)		((rdo) >> 10 & 0x3FF)
#define PD_RDO_FIXED_CURR_MINMAX(rdo)	((rdo) & 0x3FF)
#define PD_RDO_PROG_VOLTAGE(rdo)	((rdo) >> 9 & 0x7FF)
#define PD_RDO_PROG_CURR(rdo)		((rdo) & 0x7F)

#define PD_SRC_PDO_TYPE(pdo)		(((pdo) >> 30) & 3)
#define PD_SRC_PDO_TYPE_FIXED		0
#define PD_SRC_PDO_TYPE_BATTERY		1
#define PD_SRC_PDO_TYPE_VARIABLE	2
#define PD_SRC_PDO_TYPE_AUGMENTED	3

#define PD_SRC_PDO_FIXED_PR_SWAP(pdo)		(((pdo) >> 29) & 1)
#define PD_SRC_PDO_FIXED_USB_SUSP(pdo)		(((pdo) >> 28) & 1)
#define PD_SRC_PDO_FIXED_EXT_POWERED(pdo)	(((pdo) >> 27) & 1)
#define PD_SRC_PDO_FIXED_USB_COMM(pdo)		(((pdo) >> 26) & 1)
#define PD_SRC_PDO_FIXED_DR_SWAP(pdo)		(((pdo) >> 25) & 1)
#define PD_SRC_PDO_FIXED_PEAK_CURR(pdo)		(((pdo) >> 20) & 3)
#define PD_SRC_PDO_FIXED_VOLTAGE(pdo)		(((pdo) >> 10) & 0x3FF)
#define PD_SRC_PDO_FIXED_MAX_CURR(pdo)		((pdo) & 0x3FF)

#define PD_SRC_PDO_VAR_BATT_MAX_VOLT(pdo)	(((pdo) >> 20) & 0x3FF)
#define PD_SRC_PDO_VAR_BATT_MIN_VOLT(pdo)	(((pdo) >> 10) & 0x3FF)
#define PD_SRC_PDO_VAR_BATT_MAX(pdo)		((pdo) & 0x3FF)

#define PD_APDO_PPS(pdo)			(((pdo) >> 28) & 3)
#define PD_APDO_MAX_VOLT(pdo)			(((pdo) >> 17) & 0xFF)
#define PD_APDO_MIN_VOLT(pdo)			(((pdo) >> 8) & 0xFF)
#define PD_APDO_MAX_CURR(pdo)			((pdo) & 0x7F)

/* Vendor Defined Messages */
#define MAX_CRC_RECEIVE_TIME	9 /* ~(2 * tReceive_max(1.1ms) * # retry 4) */
#define MAX_VDM_RESPONSE_TIME	60 /* 2 * tVDMSenderResponse_max(30ms) */
#define MAX_VDM_BUSY_TIME	100 /* 2 * tVDMBusy (50ms) */

#define PD_SNK_PDO_FIXED(prs, hc, uc, usb_comm, drs, volt, curr) \
	(((prs) << 29) | ((hc) << 28) | ((uc) << 27) | ((usb_comm) << 26) | \
	 ((drs) << 25) | ((volt) << 10) | (curr))

/* VDM header is the first 32-bit object following the 16-bit PD header */
#define VDM_HDR_SVID(hdr)	((hdr) >> 16)
#define VDM_IS_SVDM(hdr)	((hdr) & 0x8000)
#define SVDM_HDR_VER(hdr)	(((hdr) >> 13) & 0x3)
#define SVDM_HDR_OBJ_POS(hdr)	(((hdr) >> 8) & 0x7)
#define SVDM_HDR_CMD_TYPE(hdr)	(((hdr) >> 6) & 0x3)
#define SVDM_HDR_CMD(hdr)	((hdr) & 0x1f)

#define SVDM_HDR(svid, ver, obj, cmd_type, cmd) \
	(((svid) << 16) | (1 << 15) | ((ver) << 13) \
	| ((obj) << 8) | ((cmd_type) << 6) | (cmd))

/* discover id response vdo bit fields */
#define ID_HDR_USB_HOST		BIT(31)
#define ID_HDR_USB_DEVICE	BIT(30)
#define ID_HDR_MODAL_OPR	BIT(26)
#define ID_HDR_PRODUCT_TYPE(n)	(((n) >> 27) & 0x7)
#define ID_HDR_PRODUCT_PER_MASK	(2 << 27)
#define ID_HDR_PRODUCT_HUB	1
#define ID_HDR_PRODUCT_PER	2
#define ID_HDR_PRODUCT_AMA	5
#define ID_HDR_PRODUCT_VPD	6

#define PD_MIN_SINK_CURRENT	900

static const u32 default_src_caps[] = { 0x36019096 };	/* VSafe5V @ 1.5A */
static const u32 default_snk_caps[] = { 0x2601912C };	/* VSafe5V @ 3A */

struct vdm_tx {
	u32			data[PD_MAX_DATA_OBJ];
	int			size;
};

struct rx_msg {
	u16			hdr;
	u16			data_len;	/* size of payload in bytes */
	struct list_head	entry;
	u8			payload[];
};

#define IS_DATA(m, t) ((m) && !PD_MSG_HDR_IS_EXTENDED((m)->hdr) && \
		PD_MSG_HDR_COUNT((m)->hdr) && \
		(PD_MSG_HDR_TYPE((m)->hdr) == (t)))
#define IS_CTRL(m, t) ((m) && !PD_MSG_HDR_COUNT((m)->hdr) && \
		(PD_MSG_HDR_TYPE((m)->hdr) == (t)))
#define IS_EXT(m, t) ((m) && PD_MSG_HDR_IS_EXTENDED((m)->hdr) && \
		(PD_MSG_HDR_TYPE((m)->hdr) == (t)))

struct usbpd {
	struct device		dev;
	struct workqueue_struct	*wq;
	struct work_struct	sm_work;
	struct work_struct	start_periph_work;
	struct work_struct	psy_chg_work;
	struct hrtimer		timer;
	bool			sm_queued;

	struct extcon_dev	*extcon;

	enum usbpd_state	current_state;
	bool			hard_reset_recvd;
	ktime_t			hard_reset_recvd_time;
	struct list_head	rx_q;
	spinlock_t		rx_lock;
	struct rx_msg		*rx_ext_msg;

	u32			received_pdos[PD_MAX_DATA_OBJ];
	u16			src_cap_id;
	u8			selected_pdo;
	u8			requested_pdo;
	u32			rdo;	/* can be either source or sink */
	int			current_voltage;	/* uV */
	int			requested_voltage;	/* uV */
	int			requested_current;	/* mA */
	bool			pd_connected;
	bool			in_explicit_contract;
	bool			peer_usb_comm;
	bool			peer_pr_swap;
	bool			peer_dr_swap;

	u32			sink_caps[7];
	int			num_sink_caps;

	struct power_supply	*usb_psy;
	struct power_supply	*bat_psy;
	struct notifier_block	psy_nb;
	struct iio_channel	*iio_channels[POWER_SUPPLY_IIO_PROP_MAX];

	int			bat_charge_full;
	int			bat_voltage_max;

	enum power_supply_typec_mode typec_mode;
	enum power_supply_typec_power_role forced_pr;
	bool			vbus_present;

	enum pd_spec_rev	spec_rev;
	enum data_role		current_dr;
	enum power_role		current_pr;
	bool			in_pr_swap;
	bool			pd_phy_opened;
	bool			send_request;
	struct completion	is_ready;
	struct completion	tx_chunk_request;
	u8			next_tx_chunk;

	struct typec_capability	typec_caps;
	struct typec_port	*typec_port;
	struct typec_partner	*partner;
	struct typec_partner_desc partner_desc;
	struct usb_pd_identity	partner_identity;

	struct mutex		swap_lock;
	bool			send_pr_swap;
	bool			send_dr_swap;

	struct regulator	*vbus;
	struct regulator	*vconn;
	bool			vbus_enabled;
	bool			vconn_enabled;

	u8			tx_msgid[SOPII_MSG + 1];
	u8			rx_msgid[SOPII_MSG + 1];
	int			caps_count;
	int			hard_reset_count;

	enum vdm_state		vdm_state;
	u16			*discovered_svids;
	int			num_svids;
	struct vdm_tx		*vdm_tx;
	struct vdm_tx		*vdm_tx_retry;
	struct mutex		svid_handler_lock;
	struct list_head	svid_handlers;
	ktime_t			svdm_start_time;
	bool			vdm_in_suspend;

	struct list_head	instance;

	bool		has_dp;
	u16			ss_lane_svid;

	/* ext msg support */
	bool			send_get_src_cap_ext;
	u8			src_cap_ext_db[PD_SRC_CAP_EXT_DB_LEN];
	bool			send_get_pps_status;
	u32			pps_status_db;
	bool			send_get_status;
	u8			status_db[PD_STATUS_DB_LEN];
	bool			send_get_battery_cap;
	u8			get_battery_cap_db;
	u8			battery_cap_db[PD_BATTERY_CAP_DB_LEN];
	u8			get_battery_status_db;
	bool			send_get_battery_status;
	u32			battery_sts_dobj;

	struct pd_phy_ops	*pdphy_ops;
};

static LIST_HEAD(_usbpd);	/* useful for debugging */

static const unsigned int usbpd_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_DISP_DP,
	EXTCON_NONE,
};

struct usbpd_state_handler {
	void (*enter_state)(struct usbpd *pd);
	void (*handle_state)(struct usbpd *pd, struct rx_msg *msg);
};

static void handle_vdm_tx(struct usbpd *pd, enum pd_sop_type sop_type);
static void usbpd_set_state(struct usbpd *pd, enum usbpd_state next_state);
static void handle_state_snk_wait_for_capabilities(struct usbpd *pd,
	struct rx_msg *rx_msg);
static void handle_state_prs_snk_src_source_on(struct usbpd *pd,
	struct rx_msg *rx_msg);

static const struct usbpd_state_handler state_handlers[];

int usbpd_get_psy_iio_property(struct usbpd *pd,
		enum iio_psy_property prop, union power_supply_propval *val)
{
	int ret, value;

	ret = iio_read_channel_processed(pd->iio_channels[prop], &value);
	if (ret < 0) {
		usbpd_err(&pd->dev, "failed to get IIO property: %d\n", prop);
		return ret;
	}

	val->intval = value;
	return 0;
}

int usbpd_set_psy_iio_property(struct usbpd *pd,
		enum iio_psy_property prop, union power_supply_propval *val)
{
	int ret;

	ret = iio_write_channel_raw(pd->iio_channels[prop], val->intval);
	if (ret < 0) {
		usbpd_err(&pd->dev, "failed to set IIO property: %d\n", prop);
		return ret;
	}

	return 0;
}

enum plug_orientation usbpd_get_plug_orientation(struct usbpd *pd)
{
	int ret;
	union power_supply_propval val;

	ret = usbpd_get_psy_iio_property(pd,
		POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION, &val);
	if (ret)
		return ORIENTATION_NONE;

	return val.intval;
}
EXPORT_SYMBOL(usbpd_get_plug_orientation);

static unsigned int get_connector_type(struct usbpd *pd)
{
	int ret;
	union power_supply_propval val;

	ret = usbpd_get_psy_iio_property(pd,
		POWER_SUPPLY_PROP_CONNECTOR_TYPE, &val);
	if (ret) {
		dev_err(&pd->dev, "Unable to read CONNECTOR TYPE: %d\n", ret);
		return ret;
	}

	return val.intval;
}

static inline void stop_usb_host(struct usbpd *pd)
{
	extcon_set_state_sync(pd->extcon, EXTCON_USB_HOST, 0);
}

static inline void start_usb_host(struct usbpd *pd, bool ss)
{
	enum plug_orientation cc = usbpd_get_plug_orientation(pd);
	union extcon_property_value val;

	val.intval = (cc == ORIENTATION_CC2);
	extcon_set_property(pd->extcon, EXTCON_USB_HOST,
			EXTCON_PROP_USB_TYPEC_POLARITY, val);

	val.intval = ss;
	extcon_set_property(pd->extcon, EXTCON_USB_HOST,
			EXTCON_PROP_USB_SS, val);

	extcon_set_state_sync(pd->extcon, EXTCON_USB_HOST, 1);
}

static inline void stop_usb_peripheral(struct usbpd *pd)
{
	extcon_set_state_sync(pd->extcon, EXTCON_USB, 0);
}

static inline void start_usb_peripheral(struct usbpd *pd)
{
	enum plug_orientation cc = usbpd_get_plug_orientation(pd);
	union extcon_property_value val;

	val.intval = (cc == ORIENTATION_CC2);
	extcon_set_property(pd->extcon, EXTCON_USB,
			EXTCON_PROP_USB_TYPEC_POLARITY, val);

	val.intval = 1;
	extcon_set_property(pd->extcon, EXTCON_USB, EXTCON_PROP_USB_SS, val);

	extcon_set_state_sync(pd->extcon, EXTCON_USB, 1);
}

static void start_usb_peripheral_work(struct work_struct *w)
{
	struct usbpd *pd = container_of(w, struct usbpd, start_periph_work);

	pd->current_state = PE_SNK_STARTUP;
	pd->current_pr = PR_SINK;
	pd->current_dr = DR_UFP;
	start_usb_peripheral(pd);
	typec_set_data_role(pd->typec_port, TYPEC_DEVICE);
	typec_set_pwr_role(pd->typec_port, TYPEC_SINK);
	typec_set_pwr_opmode(pd->typec_port,
			pd->typec_mode - QTI_POWER_SUPPLY_TYPEC_SOURCE_DEFAULT);
	if (!pd->partner) {
		memset(&pd->partner_identity, 0, sizeof(pd->partner_identity));
		pd->partner_desc.usb_pd = false;
		pd->partner_desc.accessory = TYPEC_ACCESSORY_NONE;
		pd->partner = typec_register_partner(pd->typec_port,
				&pd->partner_desc);
	}
}

static void start_usb_dp(struct usbpd *pd, bool ss)
{
	enum plug_orientation cc = usbpd_get_plug_orientation(pd);
	union extcon_property_value val;

	/* set state to enable to allow client can get polarity */
	extcon_set_state(pd->extcon, EXTCON_USB_HOST, 1);

	val.intval = (cc == ORIENTATION_CC2);
	extcon_set_property(pd->extcon, EXTCON_USB_HOST, EXTCON_PROP_USB_TYPEC_POLARITY, val);

	val.intval = ss  ? 1 : 0;
	extcon_set_property(pd->extcon, EXTCON_USB_HOST, EXTCON_PROP_USB_SS, val);

	extcon_set_state(pd->extcon, EXTCON_DISP_DP, false);
	extcon_set_state_sync(pd->extcon, EXTCON_DISP_DP, true);
}

static void stop_usb_dp(struct usbpd *pd)
{
	extcon_set_state_sync(pd->extcon, EXTCON_DISP_DP, false);
}

/**
 * This API allows client driver to request for releasing SS lanes. It should
 * not be called from atomic context.
 *
 * @pd - USBPD handler
 * @hdlr - client's handler
 *
 * @returns int - Success - 0, else negative error code
 */
static int usbpd_release_ss_lane(struct usbpd *pd,
				struct usbpd_svid_handler *hdlr)
{
	int ret = 0;

	if (!hdlr || !pd)
		return -EINVAL;

	usbpd_dbg(&pd->dev, "hdlr:%pK svid:%d", hdlr, hdlr->svid);
	/*
	 * If USB SS lanes are already used by one client, and other client is
	 * requesting for same or same client requesting again, return -EBUSY.
	 */
	if (pd->ss_lane_svid) {
		usbpd_dbg(&pd->dev, "-EBUSY: ss_lanes are already used by(%d)",
							pd->ss_lane_svid);
		ret = -EBUSY;
		goto err_exit;
	}

	pd->ss_lane_svid = hdlr->svid;

	start_usb_dp(pd, false);

err_exit:
	return ret;
}

static int set_power_role(struct usbpd *pd, enum power_role pr)
{
	union power_supply_propval val = {0};

	switch (pr) {
	case PR_NONE:
		val.intval = QTI_POWER_SUPPLY_TYPEC_PR_NONE;
		break;
	case PR_SINK:
		val.intval = QTI_POWER_SUPPLY_TYPEC_PR_SINK;
		break;
	case PR_SRC:
		val.intval = QTI_POWER_SUPPLY_TYPEC_PR_SOURCE;
		break;
	}

	return usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &val);
}

static struct usbpd_svid_handler *_find_svid_handler(struct usbpd *pd, u16 svid)
{
	struct usbpd_svid_handler *handler;

	list_for_each_entry(handler, &pd->svid_handlers, entry) {
		if (svid == handler->svid)
			return handler;
	}

	return NULL;
}

static struct usbpd_svid_handler *find_svid_handler(struct usbpd *pd, u16 svid)
{
	struct usbpd_svid_handler *handler;

	/* in_interrupt() == true when handling VDM RX during suspend */
	if (!in_interrupt()) {
		mutex_lock(&pd->svid_handler_lock);
		handler = _find_svid_handler(pd, svid);
		mutex_unlock(&pd->svid_handler_lock);
	} else {
		handler = _find_svid_handler(pd, svid);
	}

	return handler;
}

/* Reset protocol layer */
static inline void pd_reset_protocol(struct usbpd *pd)
{
	/*
	 * first Rx ID should be 0; set this to a sentinel of -1 so that in
	 * phy_msg_received() we can check if we had seen it before.
	 */
	memset(pd->rx_msgid, -1, sizeof(pd->rx_msgid));
	memset(pd->tx_msgid, 0, sizeof(pd->tx_msgid));
	pd->send_request = false;
	pd->send_get_status = false;
	pd->send_pr_swap = false;
	pd->send_dr_swap = false;
}

static int pd_send_msg(struct usbpd *pd, u8 msg_type, const u32 *data,
		size_t num_data, enum pd_sop_type sop)
{
	unsigned long flags;
	int ret;
	u16 hdr;

	if (pd->hard_reset_recvd)
		return -EBUSY;

	if (sop == SOP_MSG)
		hdr = PD_MSG_HDR(msg_type, pd->current_dr, pd->current_pr,
				pd->tx_msgid[sop], num_data, pd->spec_rev);
	else
		/* sending SOP'/SOP'' to a cable, PR/DR fields should be 0 */
		hdr = PD_MSG_HDR(msg_type, 0, 0, pd->tx_msgid[sop], num_data,
				pd->spec_rev);

	/* bail out and try again later if a message just arrived */
	spin_lock_irqsave(&pd->rx_lock, flags);
	if (!list_empty(&pd->rx_q)) {
		spin_unlock_irqrestore(&pd->rx_lock, flags);
		usbpd_dbg(&pd->dev, "Abort send due to pending RX\n");
		return -EBUSY;
	}
	spin_unlock_irqrestore(&pd->rx_lock, flags);

	ret = pd->pdphy_ops->write(hdr, (u8 *)data,
						num_data * sizeof(u32), sop);
	if (ret) {
		if (pd->pd_connected)
			usbpd_err(&pd->dev, "Error sending %s: %d\n",
					msg_to_string(msg_type, num_data,
							false),
					ret);
		return ret;
	}

	pd->tx_msgid[sop] = (pd->tx_msgid[sop] + 1) & PD_MAX_MSG_ID;
	return 0;
}

static int pd_send_ext_msg(struct usbpd *pd, u8 msg_type,
		const u8 *data, size_t data_len, enum pd_sop_type sop)
{
	int ret;
	size_t len_remain, chunk_len;
	u8 chunked_payload[PD_MAX_DATA_OBJ * sizeof(u32)] = {0};
	u16 hdr;
	u16 ext_hdr;
	u8 num_objs;

	if (data_len > PD_MAX_EXT_MSG_LEN) {
		usbpd_warn(&pd->dev, "Extended message length exceeds max, truncating...\n");
		data_len = PD_MAX_EXT_MSG_LEN;
	}

	pd->next_tx_chunk = 0;
	len_remain = data_len;
	do {
		ext_hdr = PD_MSG_EXT_HDR(1, pd->next_tx_chunk++, 0, data_len);
		memcpy(chunked_payload, &ext_hdr, sizeof(ext_hdr));

		chunk_len = min_t(size_t, len_remain,
				PD_MAX_EXT_MSG_LEGACY_LEN);
		memcpy(chunked_payload + sizeof(ext_hdr), data, chunk_len);

		num_objs = DIV_ROUND_UP(chunk_len + sizeof(u16), sizeof(u32));
		len_remain -= chunk_len;

		reinit_completion(&pd->tx_chunk_request);
		hdr = PD_MSG_HDR(msg_type, pd->current_dr, pd->current_pr,
				pd->tx_msgid[sop], num_objs, pd->spec_rev) |
			PD_MSG_HDR_EXTENDED;
		ret = pd->pdphy_ops->write(hdr, chunked_payload,
				num_objs * sizeof(u32), sop);
		if (ret) {
			usbpd_err(&pd->dev, "Error sending %s: %d\n",
					usbpd_ext_msg_strings[msg_type],
					ret);
			return ret;
		}

		pd->tx_msgid[sop] = (pd->tx_msgid[sop] + 1) & PD_MAX_MSG_ID;

		/* Wait for request chunk */
		if (len_remain &&
			!wait_for_completion_timeout(&pd->tx_chunk_request,
				msecs_to_jiffies(SENDER_RESPONSE_TIME))) {
			usbpd_err(&pd->dev, "Timed out waiting for chunk request\n");
			return -EPROTO;
		}
	} while (len_remain);

	return 0;
}

static int pd_select_pdo(struct usbpd *pd, int pdo_pos, int uv, int ua)
{
	int curr;
	int max_current;
	bool mismatch = false;
	u8 type;
	u32 pdo = pd->received_pdos[pdo_pos - 1];

	type = PD_SRC_PDO_TYPE(pdo);
	if (type == PD_SRC_PDO_TYPE_FIXED) {
		curr = max_current = PD_SRC_PDO_FIXED_MAX_CURR(pdo) * 10;

		/*
		 * Check if the PDO has enough current, otherwise set the
		 * Capability Mismatch flag
		 */
		if (curr < PD_MIN_SINK_CURRENT) {
			mismatch = true;
			max_current = PD_MIN_SINK_CURRENT;
		}

		pd->requested_voltage =
			PD_SRC_PDO_FIXED_VOLTAGE(pdo) * 50 * 1000;
		pd->rdo = PD_RDO_FIXED(pdo_pos, 0, mismatch, 1, 1, curr / 10,
				max_current / 10);
	} else if (type == PD_SRC_PDO_TYPE_AUGMENTED) {
		if ((uv / 100000) > PD_APDO_MAX_VOLT(pdo) ||
			(uv / 100000) < PD_APDO_MIN_VOLT(pdo) ||
			(ua / 50000) > PD_APDO_MAX_CURR(pdo) || (ua < 0)) {
			usbpd_err(&pd->dev, "uv (%d) and ua (%d) out of range of APDO\n",
					uv, ua);
			return -EINVAL;
		}

		curr = ua / 1000;
		pd->requested_voltage = uv;
		pd->rdo = PD_RDO_AUGMENTED(pdo_pos, mismatch, 1, 1,
				uv / 20000, ua / 50000);
	} else {
		usbpd_err(&pd->dev, "Only Fixed or Programmable PDOs supported\n");
		return -EOPNOTSUPP;
	}

	pd->requested_current = curr;
	pd->requested_pdo = pdo_pos;

	return 0;
}

static int pd_eval_src_caps(struct usbpd *pd)
{
	int i;
	union power_supply_propval val;
	bool pps_found = false;
	u32 first_pdo = pd->received_pdos[0];

	if (PD_SRC_PDO_TYPE(first_pdo) != PD_SRC_PDO_TYPE_FIXED) {
		usbpd_err(&pd->dev, "First src_cap invalid %08x\n", first_pdo);
		return -EINVAL;
	}

	pd->peer_usb_comm = PD_SRC_PDO_FIXED_USB_COMM(first_pdo);
	pd->peer_pr_swap = PD_SRC_PDO_FIXED_PR_SWAP(first_pdo);
	pd->peer_dr_swap = PD_SRC_PDO_FIXED_DR_SWAP(first_pdo);

	val.intval = PD_SRC_PDO_FIXED_USB_SUSP(first_pdo);
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED, &val);

	/* Check for PPS APDOs */
	if (pd->spec_rev == USBPD_REV_30) {
		for (i = 1; i < PD_MAX_DATA_OBJ; i++) {
			if ((PD_SRC_PDO_TYPE(pd->received_pdos[i]) ==
					PD_SRC_PDO_TYPE_AUGMENTED) &&
				!PD_APDO_PPS(pd->received_pdos[i])) {
				pps_found = true;
				break;
			}
		}
	}

	val.intval = pps_found ?
			QTI_POWER_SUPPLY_PD_PPS_ACTIVE :
			QTI_POWER_SUPPLY_PD_ACTIVE;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_ACTIVE, &val);

	/* First time connecting to a PD source and it supports USB data */
	if (pd->peer_usb_comm && pd->current_dr == DR_UFP && !pd->pd_connected)
		start_usb_peripheral(pd);

	/* Select the first PDO (vSafe5V) immediately. */
	pd_select_pdo(pd, 1, 0, 0);

	return 0;
}

static void pd_send_hard_reset(struct usbpd *pd)
{
	union power_supply_propval val = {0};

	usbpd_dbg(&pd->dev, "send hard reset");

	pd->hard_reset_count++;
	pd->pdphy_ops->signal(HARD_RESET_SIG);
	pd->in_pr_swap = false;
	pd->pd_connected = false;
	usbpd_set_psy_iio_property(pd, POWER_SUPPLY_PROP_PR_SWAP, &val);
}

static void kick_sm(struct usbpd *pd, int ms)
{
	pm_stay_awake(&pd->dev);
	pd->sm_queued = true;

	if (ms) {
		usbpd_dbg(&pd->dev, "delay %d ms", ms);
		hrtimer_start(&pd->timer, ms_to_ktime(ms), HRTIMER_MODE_REL);
	} else {
		queue_work(pd->wq, &pd->sm_work);
	}
}

static void phy_sig_received(struct usbpd *pd, enum pd_sig_type sig)
{
	union power_supply_propval val = {1};

	if (sig != HARD_RESET_SIG) {
		usbpd_err(&pd->dev, "invalid signal (%d) received\n", sig);
		return;
	}

	pd->hard_reset_recvd = true;
	pd->hard_reset_recvd_time = ktime_get();

	usbpd_err(&pd->dev, "hard reset received\n");

	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_IN_HARD_RESET, &val);

	kick_sm(pd, 0);
}

struct pd_request_chunk {
	struct work_struct	w;
	struct usbpd		*pd;
	u8			msg_type;
	u8			chunk_num;
	enum pd_sop_type	sop;
};

static void pd_request_chunk_work(struct work_struct *w)
{
	struct pd_request_chunk *req =
		container_of(w, struct pd_request_chunk, w);
	struct usbpd *pd = req->pd;
	unsigned long flags;
	int ret;
	u8 payload[4] = {0}; /* ext_hdr + padding */
	u16 hdr = PD_MSG_HDR(req->msg_type, pd->current_dr, pd->current_pr,
				pd->tx_msgid[req->sop], 1, pd->spec_rev)
		| PD_MSG_HDR_EXTENDED;

	*(u16 *)payload = PD_MSG_EXT_HDR(1, req->chunk_num, 1, 0);

	ret = pd->pdphy_ops->write(hdr, payload, sizeof(payload), req->sop);
	if (!ret) {
		pd->tx_msgid[req->sop] =
			(pd->tx_msgid[req->sop] + 1) & PD_MAX_MSG_ID;
	} else {
		usbpd_err(&pd->dev, "could not send chunk request\n");

		/* queue what we have anyway */
		spin_lock_irqsave(&pd->rx_lock, flags);
		list_add_tail(&pd->rx_ext_msg->entry, &pd->rx_q);
		spin_unlock_irqrestore(&pd->rx_lock, flags);

		pd->rx_ext_msg = NULL;
	}

	kfree(req);
}

static struct rx_msg *pd_ext_msg_received(struct usbpd *pd, u16 header, u8 *buf,
		size_t len, enum pd_sop_type sop)
{
	struct rx_msg *rx_msg;
	u16 bytes_to_copy;
	u16 ext_hdr = *(u16 *)buf;
	u8 chunk_num;

	if (!PD_MSG_EXT_HDR_IS_CHUNKED(ext_hdr)) {
		usbpd_err(&pd->dev, "unchunked extended messages unsupported\n");
		return NULL;
	}

	/* request for next Tx chunk */
	if (PD_MSG_EXT_HDR_REQ_CHUNK(ext_hdr)) {
		if (PD_MSG_EXT_HDR_DATA_SIZE(ext_hdr) ||
			PD_MSG_EXT_HDR_CHUNK_NUM(ext_hdr) !=
				pd->next_tx_chunk) {
			usbpd_err(&pd->dev, "invalid request chunk ext header 0x%02x\n",
					ext_hdr);
			return NULL;
		}

		if (!completion_done(&pd->tx_chunk_request))
			complete(&pd->tx_chunk_request);

		return NULL;
	}

	chunk_num = PD_MSG_EXT_HDR_CHUNK_NUM(ext_hdr);
	if (!chunk_num) {
		/* allocate new message if first chunk */
		rx_msg = kzalloc(sizeof(*rx_msg) +
				PD_MSG_EXT_HDR_DATA_SIZE(ext_hdr),
				GFP_ATOMIC);
		if (!rx_msg)
			return NULL;

		rx_msg->hdr = header;
		rx_msg->data_len = PD_MSG_EXT_HDR_DATA_SIZE(ext_hdr);

		if (rx_msg->data_len > PD_MAX_EXT_MSG_LEN) {
			usbpd_warn(&pd->dev, "Extended message length exceeds max, truncating...\n");
			rx_msg->data_len = PD_MAX_EXT_MSG_LEN;
		}
	} else {
		if (!pd->rx_ext_msg) {
			usbpd_err(&pd->dev, "missing first rx_ext_msg chunk\n");
			return NULL;
		}

		rx_msg = pd->rx_ext_msg;
	}

	/*
	 * The amount to copy is derived as follows:
	 *
	 * - if extended data_len < 26, then copy data_len bytes
	 * - for chunks 0..N-2, copy 26 bytes
	 * - for the last chunk (N-1), copy the remainder
	 */
	bytes_to_copy =
		min((rx_msg->data_len - chunk_num * PD_MAX_EXT_MSG_LEGACY_LEN),
			PD_MAX_EXT_MSG_LEGACY_LEN);

	/* check against received length to avoid overrun */
	if (bytes_to_copy > len - sizeof(ext_hdr)) {
		usbpd_warn(&pd->dev, "not enough bytes in chunk, expected:%u received:%lu\n",
			bytes_to_copy, len - sizeof(ext_hdr));
		bytes_to_copy = len - sizeof(ext_hdr);
	}

	memcpy(rx_msg->payload + chunk_num * PD_MAX_EXT_MSG_LEGACY_LEN, buf + 2,
			bytes_to_copy);

	/* request next chunk? */
	if ((rx_msg->data_len - chunk_num * PD_MAX_EXT_MSG_LEGACY_LEN) >
			PD_MAX_EXT_MSG_LEGACY_LEN) {
		struct pd_request_chunk *req;

		if (pd->rx_ext_msg && pd->rx_ext_msg != rx_msg) {
			usbpd_dbg(&pd->dev, "stale previous rx_ext_msg?\n");
			kfree(pd->rx_ext_msg);
		}

		pd->rx_ext_msg = rx_msg;

		req = kzalloc(sizeof(*req), GFP_ATOMIC);
		if (!req)
			goto queue_rx; /* return what we have anyway */

		INIT_WORK(&req->w, pd_request_chunk_work);
		req->pd = pd;
		req->msg_type = PD_MSG_HDR_TYPE(header);
		req->chunk_num = chunk_num + 1;
		req->sop = sop;
		queue_work(pd->wq, &req->w);

		return NULL;
	}

queue_rx:
	pd->rx_ext_msg = NULL;
	return rx_msg;	/* queue it for usbpd_sm */
}

static void handle_vdm_rx(struct usbpd *pd, struct rx_msg *rx_msg);

static void phy_msg_received(struct usbpd *pd, enum pd_sop_type sop,
		u8 *buf, size_t len)
{
	struct rx_msg *rx_msg;
	unsigned long flags;
	u16 header;
	u8 msg_type, num_objs;

	if (sop == SOPII_MSG) {
		usbpd_err(&pd->dev, "only SOP/SOP' supported\n");
		return;
	}

	if (len < 2) {
		usbpd_err(&pd->dev, "invalid message received, len=%zd\n", len);
		return;
	}

	header = *((u16 *)buf);
	buf += sizeof(u16);
	len -= sizeof(u16);

	if (len % 4 != 0) {
		usbpd_err(&pd->dev, "len=%zd not multiple of 4\n", len);
		return;
	}

	/* if MSGID already seen, discard */
	if (PD_MSG_HDR_ID(header) == pd->rx_msgid[sop] &&
			PD_MSG_HDR_TYPE(header) != MSG_SOFT_RESET) {
		usbpd_dbg(&pd->dev, "MessageID already seen, discarding\n");
		return;
	}

	pd->rx_msgid[sop] = PD_MSG_HDR_ID(header);

	/* discard Pings */
	if (PD_MSG_HDR_TYPE(header) == MSG_PING && !len)
		return;

	/* check header's count field to see if it matches len */
	if (PD_MSG_HDR_COUNT(header) != (len / 4)) {
		usbpd_err(&pd->dev, "header count (%d) mismatch, len=%zd\n",
				PD_MSG_HDR_COUNT(header), len);
		return;
	}

	/* if spec rev differs (i.e. is older), update PHY */
	if (PD_MSG_HDR_REV(header) < pd->spec_rev)
		pd->spec_rev = PD_MSG_HDR_REV(header);

	msg_type = PD_MSG_HDR_TYPE(header);
	num_objs = PD_MSG_HDR_COUNT(header);
	usbpd_dbg(&pd->dev, "%s type(%d) num_objs(%d)\n",
			msg_to_string(msg_type, num_objs,
				PD_MSG_HDR_IS_EXTENDED(header)),
			msg_type, num_objs);

	if (!PD_MSG_HDR_IS_EXTENDED(header)) {
		rx_msg = kzalloc(sizeof(*rx_msg) + len, GFP_ATOMIC);
		if (!rx_msg)
			return;

		rx_msg->hdr = header;
		rx_msg->data_len = len;
		memcpy(rx_msg->payload, buf, len);
	} else {
		rx_msg = pd_ext_msg_received(pd, header, buf, len, sop);
		if (!rx_msg)
			return;
	}

	if (pd->vdm_in_suspend && msg_type == MSG_VDM) {
		usbpd_dbg(&pd->dev, "Skip wq and handle VDM directly\n");
		handle_vdm_rx(pd, rx_msg);
		kfree(rx_msg);
		return;
	}

	spin_lock_irqsave(&pd->rx_lock, flags);
	list_add_tail(&rx_msg->entry, &pd->rx_q);
	spin_unlock_irqrestore(&pd->rx_lock, flags);

	if (!work_busy(&pd->sm_work))
		kick_sm(pd, 0);
	else
		usbpd_dbg(&pd->dev, "usbpd_sm already running\n");
}

static void phy_shutdown(struct usbpd *pd)
{
	usbpd_dbg(&pd->dev, "shutdown");

	if (pd->vconn_enabled) {
		regulator_disable(pd->vconn);
		pd->vconn_enabled = false;
	}

	if (pd->vbus_enabled) {
		regulator_disable(pd->vbus);
		pd->vbus_enabled = false;
	}
}

static enum hrtimer_restart pd_timeout(struct hrtimer *timer)
{
	struct usbpd *pd = container_of(timer, struct usbpd, timer);

	queue_work(pd->wq, &pd->sm_work);

	return HRTIMER_NORESTART;
}

static void log_decoded_request(struct usbpd *pd, u32 rdo)
{
	const u32 *pdos;
	int pos = PD_RDO_OBJ_POS(rdo);
	int type;

	usbpd_dbg(&pd->dev, "RDO: 0x%08x\n", pd->rdo);

	if (pd->current_pr == PR_SINK)
		pdos = pd->received_pdos;
	else
		pdos = default_src_caps;

	type = PD_SRC_PDO_TYPE(pdos[pos - 1]);

	switch (type) {
	case PD_SRC_PDO_TYPE_FIXED:
	case PD_SRC_PDO_TYPE_VARIABLE:
		usbpd_dbg(&pd->dev, "Request Fixed/Variable PDO:%d Volt:%dmV OpCurr:%dmA Curr:%dmA\n",
				pos,
				PD_SRC_PDO_FIXED_VOLTAGE(pdos[pos - 1]) * 50,
				PD_RDO_FIXED_CURR(rdo) * 10,
				PD_RDO_FIXED_CURR_MINMAX(rdo) * 10);
		break;

	case PD_SRC_PDO_TYPE_BATTERY:
		usbpd_dbg(&pd->dev, "Request Battery PDO:%d OpPow:%dmW Pow:%dmW\n",
				pos,
				PD_RDO_FIXED_CURR(rdo) * 250,
				PD_RDO_FIXED_CURR_MINMAX(rdo) * 250);
		break;

	case PD_SRC_PDO_TYPE_AUGMENTED:
		usbpd_dbg(&pd->dev, "Request PPS PDO:%d Volt:%dmV Curr:%dmA\n",
				pos,
				PD_RDO_PROG_VOLTAGE(rdo) * 20,
				PD_RDO_PROG_CURR(rdo) * 50);
		break;
	}
}

static bool in_src_ams(struct usbpd *pd)
{
	union power_supply_propval val = {0};

	if (pd->current_pr != PR_SRC)
		return false;

	if (pd->spec_rev != USBPD_REV_30)
		return true;

	usbpd_get_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_SRC_RP, &val);

	return val.intval == 1;
}

static void start_src_ams(struct usbpd *pd, bool ams)
{
	union power_supply_propval val = {0};

	if (pd->current_pr != PR_SRC || pd->spec_rev < USBPD_REV_30)
		return;

	usbpd_dbg(&pd->dev, "Set Rp to %s\n", ams ? "1.5A" : "3A");

	val.intval = ams ? 1 : 2; /* SinkTxNG / SinkTxOK */
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_SRC_RP, &val);

	if (ams)
		kick_sm(pd, SINK_TX_TIME);
}

int usbpd_register_svid(struct usbpd *pd, struct usbpd_svid_handler *hdlr)
{
	if (find_svid_handler(pd, hdlr->svid)) {
		usbpd_err(&pd->dev, "SVID 0x%04x already registered\n",
				hdlr->svid);
		return -EINVAL;
	}

	/* require connect/disconnect callbacks be implemented */
	if (!hdlr->connect || !hdlr->disconnect) {
		usbpd_err(&pd->dev, "SVID 0x%04x connect/disconnect must be non-NULL\n",
				hdlr->svid);
		return -EINVAL;
	}

	usbpd_dbg(&pd->dev, "registered handler(%pK) for SVID 0x%04x\n",
							hdlr, hdlr->svid);
	mutex_lock(&pd->svid_handler_lock);
	list_add_tail(&hdlr->entry, &pd->svid_handlers);
	mutex_unlock(&pd->svid_handler_lock);
	hdlr->request_usb_ss_lane = usbpd_release_ss_lane;

	/* already connected with this SVID discovered? */
	if (pd->vdm_state >= DISCOVERED_SVIDS) {
		int i;

		for (i = 0; i < pd->num_svids; i++) {
			if (pd->discovered_svids[i] == hdlr->svid) {
				usbpd_dbg(&pd->dev, "Notify SVID: 0x%04x disconnect\n",
						hdlr->svid);
				hdlr->connect(hdlr, pd->peer_usb_comm);
				hdlr->discovered = true;
				break;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(usbpd_register_svid);

void usbpd_unregister_svid(struct usbpd *pd, struct usbpd_svid_handler *hdlr)
{

	usbpd_dbg(&pd->dev, "unregistered handler(%pK) for SVID 0x%04x\n",
							hdlr, hdlr->svid);
	mutex_lock(&pd->svid_handler_lock);
	list_del_init(&hdlr->entry);
	mutex_unlock(&pd->svid_handler_lock);
}
EXPORT_SYMBOL(usbpd_unregister_svid);

int usbpd_send_vdm(struct usbpd *pd, u32 vdm_hdr, const u32 *vdos, int num_vdos)
{
	struct vdm_tx *vdm_tx;

	if (pd->vdm_tx) {
		usbpd_warn(&pd->dev, "Discarding previously queued VDM tx (SVID:0x%04x)\n",
				VDM_HDR_SVID(pd->vdm_tx->data[0]));

		kfree(pd->vdm_tx);
		pd->vdm_tx = NULL;
	}

	vdm_tx = kzalloc(sizeof(*vdm_tx), GFP_KERNEL);
	if (!vdm_tx)
		return -ENOMEM;

	vdm_tx->data[0] = vdm_hdr;
	if (vdos && num_vdos)
		memcpy(&vdm_tx->data[1], vdos, num_vdos * sizeof(u32));
	vdm_tx->size = num_vdos + 1; /* include the header */

	/* VDM will get sent in PE_SRC/SNK_READY state handling */
	pd->vdm_tx = vdm_tx;
	pd->vdm_in_suspend = false;

	/* slight delay before queuing to prioritize handling of incoming VDM */
	if (pd->in_explicit_contract)
		kick_sm(pd, 2);

	return 0;
}
EXPORT_SYMBOL(usbpd_send_vdm);

int usbpd_send_svdm(struct usbpd *pd, u16 svid, u8 cmd,
		enum usbpd_svdm_cmd_type cmd_type, int obj_pos,
		const u32 *vdos, int num_vdos)
{
	u32 svdm_hdr = SVDM_HDR(svid, pd->spec_rev == USBPD_REV_30 ? 1 : 0,
			obj_pos, cmd_type, cmd);

	usbpd_dbg(&pd->dev, "VDM tx: svid:%04x ver:%d obj_pos:%d cmd:%x cmd_type:%x svdm_hdr:%x\n",
			svid, pd->spec_rev == USBPD_REV_30 ? 1 : 0, obj_pos,
			cmd, cmd_type, svdm_hdr);

	return usbpd_send_vdm(pd, svdm_hdr, vdos, num_vdos);
}
EXPORT_SYMBOL(usbpd_send_svdm);

void usbpd_vdm_in_suspend(struct usbpd *pd, bool in_suspend)
{
	usbpd_dbg(&pd->dev, "VDM in_suspend:%d\n", in_suspend);

	pd->vdm_in_suspend = in_suspend;
}
EXPORT_SYMBOL(usbpd_vdm_in_suspend);

static void handle_vdm_resp_ack(struct usbpd *pd, u32 *vdos, u8 num_vdos,
	u16 vdm_hdr)
{
	int ret, i;
	u16 svid, *psvid;
	u8 cmd = SVDM_HDR_CMD(vdm_hdr);
	struct usbpd_svid_handler *handler;

	switch (cmd) {
	case USBPD_SVDM_DISCOVER_IDENTITY:
		kfree(pd->vdm_tx_retry);
		pd->vdm_tx_retry = NULL;

		if (!num_vdos) {
			usbpd_dbg(&pd->dev, "Discarding Discover ID response with no VDOs\n");
			break;
		}

		if (ID_HDR_PRODUCT_TYPE(vdos[0]) == ID_HDR_PRODUCT_VPD) {
			usbpd_dbg(&pd->dev, "VPD detected turn off vbus\n");

			if (pd->vbus_enabled) {
				ret = regulator_disable(pd->vbus);
				if (ret)
					usbpd_err(&pd->dev, "Err disabling vbus (%d)\n",
							ret);
				else
					pd->vbus_enabled = false;
			}
		}

		if (!pd->in_explicit_contract)
			break;

		if (SVDM_HDR_OBJ_POS(vdm_hdr) != 0) {
			usbpd_dbg(&pd->dev, "Discarding Discover ID response with incorrect object position:%d\n",
					SVDM_HDR_OBJ_POS(vdm_hdr));
			break;
		}

		pd->vdm_state = DISCOVERED_ID;
		usbpd_send_svdm(pd, USBPD_SID,
				USBPD_SVDM_DISCOVER_SVIDS,
				SVDM_CMD_TYPE_INITIATOR, 0, NULL, 0);
		break;

	case USBPD_SVDM_DISCOVER_SVIDS:
		pd->vdm_state = DISCOVERED_SVIDS;

		kfree(pd->vdm_tx_retry);
		pd->vdm_tx_retry = NULL;

		if (!pd->discovered_svids) {
			pd->num_svids = 2 * num_vdos;
			pd->discovered_svids = kcalloc(pd->num_svids,
							sizeof(u16),
							GFP_KERNEL);
			if (!pd->discovered_svids)
				break;

			psvid = pd->discovered_svids;
		} else { /* handle > 12 SVIDs */
			void *ptr;
			size_t oldsize = pd->num_svids * sizeof(u16);
			size_t newsize = oldsize +
					(2 * num_vdos * sizeof(u16));

			ptr = krealloc(pd->discovered_svids, newsize,
					GFP_KERNEL);
			if (!ptr)
				break;

			pd->discovered_svids = ptr;
			psvid = pd->discovered_svids + pd->num_svids;
			memset(psvid, 0, (2 * num_vdos));
			pd->num_svids += 2 * num_vdos;
		}

		/* convert 32-bit VDOs to list of 16-bit SVIDs */
		for (i = 0; i < num_vdos * 2; i++) {
			/*
			 * Within each 32-bit VDO,
			 *    SVID[i]: upper 16-bits
			 *    SVID[i+1]: lower 16-bits
			 * where i is even.
			 */
			if (!(i & 1))
				svid = vdos[i >> 1] >> 16;
			else
				svid = vdos[i >> 1] & 0xFFFF;

			/*
			 * There are some devices that incorrectly
			 * swap the order of SVIDs within a VDO. So in
			 * case of an odd-number of SVIDs it could end
			 * up with SVID[i] as 0 while SVID[i+1] is
			 * non-zero. Just skip over the zero ones.
			 */
			if (svid) {
				usbpd_dbg(&pd->dev, "Discovered SVID: 0x%04x\n",
						svid);
				*psvid++ = svid;
			}
		}

		/* if more than 12 SVIDs, resend the request */
		if (num_vdos == 6 && vdos[5] != 0) {
			usbpd_send_svdm(pd, USBPD_SID,
					USBPD_SVDM_DISCOVER_SVIDS,
					SVDM_CMD_TYPE_INITIATOR, 0,
					NULL, 0);
			break;
		}

		/* now that all SVIDs are discovered, notify handlers */
		for (i = 0; i < pd->num_svids; i++) {
			svid = pd->discovered_svids[i];
			if (svid) {
				handler = find_svid_handler(pd, svid);
				if (handler) {
					usbpd_dbg(&pd->dev, "Notify SVID: 0x%04x discovered\n",
							handler->svid);
					handler->connect(handler,
							pd->peer_usb_comm);
					handler->discovered = true;
				}
			}
		}
		break;

	default:
		usbpd_dbg(&pd->dev, "unhandled ACK for command:0x%x\n",
				cmd);
		break;
	}

}

static void handle_vdm_rx(struct usbpd *pd, struct rx_msg *rx_msg)
{
	int ret;
	u32 vdm_hdr =
	rx_msg->data_len >= sizeof(u32) ? ((u32 *)rx_msg->payload)[0] : 0;

	u32 *vdos = (u32 *)&rx_msg->payload[sizeof(u32)];
	u16 svid = VDM_HDR_SVID(vdm_hdr);

	u8 num_vdos = PD_MSG_HDR_COUNT(rx_msg->hdr) - 1;
	u8 cmd = SVDM_HDR_CMD(vdm_hdr);
	u8 cmd_type = SVDM_HDR_CMD_TYPE(vdm_hdr);
	struct usbpd_svid_handler *handler;
	ktime_t recvd_time = ktime_get();

	usbpd_dbg(&pd->dev,
			"VDM rx: svid:%04x cmd:%x cmd_type:%x vdm_hdr:%x has_dp: %s\n",
			svid, cmd, cmd_type, vdm_hdr,
			pd->has_dp ? "true" : "false");

	if ((svid == 0xFF01) && (!pd->has_dp)) {
		pd->has_dp = true;
		/* policy engine based display driver only support release 4 lanes,
		 * it is not good, as from usb view, for two lanes display,
		 * there is extra operation except phy.
		 */
		start_usb_dp(pd, true);
	}

	/* if it's a supported SVID, pass the message to the handler */
	handler = find_svid_handler(pd, svid);

	/* Unstructured VDM */
	if (!VDM_IS_SVDM(vdm_hdr)) {
		if (handler && handler->vdm_received) {
			handler->vdm_received(handler, vdm_hdr, vdos, num_vdos);
		} else if (pd->spec_rev == USBPD_REV_30) {
			ret = pd_send_msg(pd, MSG_NOT_SUPPORTED, NULL, 0,
					SOP_MSG);
			if (ret)
				usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		}
		return;
	}

	if (SVDM_HDR_VER(vdm_hdr) > 1)
		usbpd_dbg(&pd->dev, "Received SVDM with unsupported version:%d\n",
				SVDM_HDR_VER(vdm_hdr));

	if (cmd_type != SVDM_CMD_TYPE_INITIATOR &&
			pd->current_state != PE_SRC_STARTUP_WAIT_FOR_VDM_RESP)
		start_src_ams(pd, false);

	if (handler && handler->svdm_received) {
		handler->svdm_received(handler, cmd, cmd_type, vdos, num_vdos);

		/* handle any previously queued TX */
		if (pd->vdm_tx && !pd->sm_queued)
			kick_sm(pd, 0);

		return;
	}

	/* Standard Discovery or unhandled messages go here */
	switch (cmd_type) {
	case SVDM_CMD_TYPE_INITIATOR:
		if (cmd != USBPD_SVDM_ATTENTION) {
			if (pd->spec_rev == USBPD_REV_30) {
				ret = pd_send_msg(pd, MSG_NOT_SUPPORTED, NULL,
						0, SOP_MSG);
				if (ret)
					usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			}
		}
		break;

	case SVDM_CMD_TYPE_RESP_ACK:
		if (svid != USBPD_SID) {
			usbpd_err(&pd->dev, "unhandled ACK for SVID:0x%x\n",
					svid);
			break;
		}

		if (ktime_ms_delta(recvd_time, pd->svdm_start_time) >
				SENDER_RESPONSE_TIME) {
			usbpd_dbg(&pd->dev, "Discarding delayed SVDM response due to timeout\n");
			break;
		}

		handle_vdm_resp_ack(pd, vdos, num_vdos, vdm_hdr);
		break;

	case SVDM_CMD_TYPE_RESP_NAK:
		usbpd_info(&pd->dev, "VDM NAK received for SVID:0x%04x command:0x%x\n",
				svid, cmd);
		break;

	case SVDM_CMD_TYPE_RESP_BUSY:
		switch (cmd) {
		case USBPD_SVDM_DISCOVER_IDENTITY:
		case USBPD_SVDM_DISCOVER_SVIDS:
			if (!pd->vdm_tx_retry) {
				usbpd_err(&pd->dev, "Discover command %d VDM was unexpectedly freed\n",
						cmd);
				break;
			}

			/* wait tVDMBusy, then retry */
			pd->vdm_tx = pd->vdm_tx_retry;
			pd->vdm_tx_retry = NULL;
			kick_sm(pd, VDM_BUSY_TIME);
			break;
		default:
			break;
		}
		break;
	}
}

static void handle_vdm_tx(struct usbpd *pd, enum pd_sop_type sop_type)
{
	u32 vdm_hdr;
	int ret;

	mutex_lock(&pd->svid_handler_lock);
	if (!pd->vdm_tx) {
		mutex_unlock(&pd->svid_handler_lock);
		return;
	}

	/* only send one VDM at a time */
	vdm_hdr = pd->vdm_tx->data[0];

	/*
	 * PD 3.0: For initiated SVDMs, source must first ensure Rp is set
	 * to SinkTxNG to indicate the start of an AMS
	 */
	if (VDM_IS_SVDM(vdm_hdr) &&
		SVDM_HDR_CMD_TYPE(vdm_hdr) == SVDM_CMD_TYPE_INITIATOR &&
		pd->current_pr == PR_SRC && !in_src_ams(pd)) {
		/* Set SinkTxNG and reschedule sm_work to send again */
		start_src_ams(pd, true);
		mutex_unlock(&pd->svid_handler_lock);
		return;
	}

	ret = pd_send_msg(pd, MSG_VDM, pd->vdm_tx->data,
			pd->vdm_tx->size, sop_type);
	if (ret) {
		usbpd_err(&pd->dev, "Error (%d) sending VDM command %d\n",
				ret, SVDM_HDR_CMD(pd->vdm_tx->data[0]));

		mutex_unlock(&pd->svid_handler_lock);
		/* retry when hitting PE_SRC/SNK_Ready again */
		if (ret != -EBUSY && sop_type == SOP_MSG)
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);

		return;
	}

	/* start tVDMSenderResponse timer */
	if (VDM_IS_SVDM(vdm_hdr) &&
		SVDM_HDR_CMD_TYPE(vdm_hdr) == SVDM_CMD_TYPE_INITIATOR) {
		pd->svdm_start_time = ktime_get();
	}

	/*
	 * special case: keep initiated Discover ID/SVIDs
	 * around in case we need to re-try when receiving BUSY
	 */
	if (VDM_IS_SVDM(vdm_hdr) &&
		SVDM_HDR_CMD_TYPE(vdm_hdr) == SVDM_CMD_TYPE_INITIATOR &&
		SVDM_HDR_CMD(vdm_hdr) <= USBPD_SVDM_DISCOVER_SVIDS) {
		if (pd->vdm_tx_retry) {
			usbpd_dbg(&pd->dev, "Previous Discover VDM command %d not ACKed/NAKed\n",
				SVDM_HDR_CMD(
					pd->vdm_tx_retry->data[0]));
			kfree(pd->vdm_tx_retry);
		}
		pd->vdm_tx_retry = pd->vdm_tx;
	} else {
		kfree(pd->vdm_tx);
	}

	pd->vdm_tx = NULL;
	mutex_unlock(&pd->svid_handler_lock);
}

static void reset_vdm_state(struct usbpd *pd)
{
	struct usbpd_svid_handler *handler;

	mutex_lock(&pd->svid_handler_lock);
	list_for_each_entry(handler, &pd->svid_handlers, entry) {
		if (handler->discovered) {
			usbpd_dbg(&pd->dev, "Notify SVID: 0x%04x disconnect\n",
					handler->svid);
			handler->disconnect(handler);
			handler->discovered = false;
		}
	}

	stop_usb_dp(pd);

	pd->vdm_state = VDM_NONE;
	kfree(pd->vdm_tx_retry);
	pd->vdm_tx_retry = NULL;
	kfree(pd->discovered_svids);
	pd->discovered_svids = NULL;
	pd->num_svids = 0;
	kfree(pd->vdm_tx);
	pd->vdm_tx = NULL;
	pd->ss_lane_svid = 0x0;
	pd->vdm_in_suspend = false;
	mutex_unlock(&pd->svid_handler_lock);
}

static void handle_get_src_cap_extended(struct usbpd *pd)
{
	int ret;
	struct {
		u16 vid;
		u16 pid;
		u32 xid;
		u8  fw_version;
		u8  hw_version;
		u8  voltage_reg;
		u8  holdup_time;
		u8  compliance;
		u8  touch_current;
		u16 peak_current1;
		u16 peak_current2;
		u16 peak_current3;
		u8  touch_temp;
		u8  source_inputs;
		u8  num_batt;
		u8  pdp;
	} __packed caps = {0};

	caps.vid = 0x5c6;
	caps.num_batt = 1;
	caps.pdp = 5 * PD_SRC_PDO_FIXED_MAX_CURR(default_src_caps[0]) / 100;

	ret = pd_send_ext_msg(pd, MSG_SOURCE_CAPABILITIES_EXTENDED, (u8 *)&caps,
			sizeof(caps), SOP_MSG);
	if (ret)
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
}

static void handle_get_battery_cap(struct usbpd *pd, struct rx_msg *rx_msg)
{
	int ret;
	u8 bat_num;
	struct {
		u16 vid;
		u16 pid;
		u16 capacity;
		u16 last_full;
		u8  type;
	} __packed bcdb = {0, 0, 0xffff, 0xffff, 0};

	if (rx_msg->data_len != 1) {
		usbpd_err(&pd->dev, "Invalid payload size: %d\n",
				rx_msg->data_len);
		return;
	}

	bat_num = rx_msg->payload[0];

	if (bat_num || !pd->bat_psy) {
		usbpd_warn(&pd->dev, "Battery %d unsupported\n", bat_num);
		bcdb.type = BIT(0); /* invalid */
		goto send;
	}

	bcdb.capacity = ((pd->bat_charge_full / 1000) *
			(pd->bat_voltage_max / 1000)) / 100000;
	/* fix me */
	bcdb.last_full = ((pd->bat_charge_full / 1000) *
			(pd->bat_voltage_max / 1000)) / 100000;
send:
	ret = pd_send_ext_msg(pd, MSG_BATTERY_CAPABILITIES, (u8 *)&bcdb,
			sizeof(bcdb), SOP_MSG);
	if (ret)
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
}

static void handle_get_battery_status(struct usbpd *pd, struct rx_msg *rx_msg)
{
	int ret;
	int cap;
	union power_supply_propval val = {0};
	u8 bat_num;
	u32 bsdo = 0xffff0000;

	if (rx_msg->data_len != 1) {
		usbpd_err(&pd->dev, "Invalid payload size: %d\n",
				rx_msg->data_len);
		return;
	}

	bat_num = rx_msg->payload[0];

	if (bat_num || !pd->bat_psy) {
		usbpd_warn(&pd->dev, "Battery %d unsupported\n", bat_num);
		bsdo |= BIT(8); /* invalid */
		goto send;
	}

	ret = power_supply_get_property(pd->bat_psy, POWER_SUPPLY_PROP_PRESENT,
			&val);
	if (ret || !val.intval)
		goto send;

	bsdo |= BIT(9);

	ret = power_supply_get_property(pd->bat_psy, POWER_SUPPLY_PROP_STATUS,
			&val);
	if (!ret) {
		switch (val.intval) {
		case POWER_SUPPLY_STATUS_CHARGING:
			break;
		case POWER_SUPPLY_STATUS_DISCHARGING:
			bsdo |= (1 << 10);
			break;
		default:
			bsdo |= (2 << 10);
			break;
		}
	}

	/* state of charge */
	ret = power_supply_get_property(pd->bat_psy,
			POWER_SUPPLY_PROP_CAPACITY, &val);
	if (ret)
		goto send;
	cap = val.intval;

	bsdo &= 0xffff;
	bsdo |= ((cap * (pd->bat_charge_full / 1000) *
			(pd->bat_voltage_max / 1000)) / 10000000) << 16;
send:
	ret = pd_send_msg(pd, MSG_BATTERY_STATUS, &bsdo, 1, SOP_MSG);
	if (ret)
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
}

static void dr_swap(struct usbpd *pd)
{
	reset_vdm_state(pd);
	usbpd_dbg(&pd->dev, "%s: current_dr(%d)\n", __func__, pd->current_dr);

	if (pd->current_dr == DR_DFP) {
		pd->current_dr = DR_UFP;
		pd->pdphy_ops->update_roles(pd->current_dr, pd->current_pr);

		stop_usb_host(pd);
		if (pd->peer_usb_comm)
			start_usb_peripheral(pd);
		typec_set_data_role(pd->typec_port, TYPEC_DEVICE);
	} else if (pd->current_dr == DR_UFP) {
		pd->current_dr = DR_DFP;
		pd->pdphy_ops->update_roles(pd->current_dr, pd->current_pr);

		stop_usb_peripheral(pd);
		if (pd->peer_usb_comm)
			start_usb_host(pd, true);

		typec_set_data_role(pd->typec_port, TYPEC_HOST);

		usbpd_send_svdm(pd, USBPD_SID, USBPD_SVDM_DISCOVER_IDENTITY,
				SVDM_CMD_TYPE_INITIATOR, 0, NULL, 0);
	}
}

static void vconn_swap(struct usbpd *pd)
{
	int ret;

	if (pd->vconn_enabled) {
		pd->pdphy_ops->update_frame_filter(FRAME_FILTER_EN_SOP |
					   FRAME_FILTER_EN_HARD_RESET);

		pd->current_state = PE_VCS_WAIT_FOR_VCONN;
		kick_sm(pd, VCONN_ON_TIME);
	} else {
		if (!pd->vconn) {
			pd->vconn = devm_regulator_get(pd->dev.parent, "vconn");
			if (IS_ERR(pd->vconn)) {
				usbpd_err(&pd->dev, "Unable to get vconn\n");
				return;
			}
		}
		ret = regulator_enable(pd->vconn);
		if (ret) {
			usbpd_err(&pd->dev, "Unable to enable vconn\n");
			return;
		}

		pd->vconn_enabled = true;

		pd->pdphy_ops->update_frame_filter(FRAME_FILTER_EN_SOP |
					   FRAME_FILTER_EN_SOPI |
					   FRAME_FILTER_EN_HARD_RESET);

		/*
		 * Small delay to ensure Vconn has ramped up. This is well
		 * below tVCONNSourceOn (100ms) so we still send PS_RDY within
		 * the allowed time.
		 */
		usleep_range(5000, 10000);

		ret = pd_send_msg(pd, MSG_PS_RDY, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}
	}
}

static int enable_vbus(struct usbpd *pd)
{
	union power_supply_propval val = {0};
	int count = 100;
	int ret;

	/*
	 * Check to make sure there's no lingering charge on
	 * VBUS before enabling it as a source. If so poll here
	 * until it goes below VSafe0V (0.8V) before proceeding.
	 */
	while (count--) {
		ret = power_supply_get_property(pd->usb_psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
		if (ret || val.intval <= 800000)
			break;
		usleep_range(20000, 30000);
	}

	if (count < 99)
		msleep(100);	/* need to wait an additional tCCDebounce */

	if (!pd->vbus) {
		pd->vbus = devm_regulator_get(pd->dev.parent, "vbus");
		if (IS_ERR(pd->vbus)) {
			usbpd_err(&pd->dev, "Unable to get vbus\n");
			return -EAGAIN;
		}
	}
	ret = regulator_enable(pd->vbus);
	if (ret)
		usbpd_err(&pd->dev, "Unable to enable vbus (%d)\n", ret);
	else
		pd->vbus_enabled = true;

	count = 10;
	/*
	 * Check to make sure VBUS voltage reaches above Vsafe5Vmin (4.75v)
	 * before proceeding.
	 */
	while (count--) {
		ret = power_supply_get_property(pd->usb_psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
		if (ret || val.intval >= 4750000) /*vsafe5Vmin*/
			break;
		usleep_range(10000, 12000); /* Delay between two reads */
	}

	if (ret)
		msleep(100); /* Delay to wait for VBUS ramp up if read fails */

	return ret;
}

static inline void rx_msg_cleanup(struct usbpd *pd)
{
	struct rx_msg *msg, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&pd->rx_lock, flags);
	list_for_each_entry_safe(msg, tmp, &pd->rx_q, entry) {
		list_del(&msg->entry);
		kfree(msg);
	}
	spin_unlock_irqrestore(&pd->rx_lock, flags);
}

/* For PD 3.0, check SinkTxOk before allowing initiating AMS */
static inline bool is_sink_tx_ok(struct usbpd *pd)
{
	if (pd->spec_rev == USBPD_REV_30)
		return pd->typec_mode == QTI_POWER_SUPPLY_TYPEC_SOURCE_HIGH;

	return true;
}

static void handle_state_unknown(struct usbpd *pd, struct rx_msg *rx_msg)
{
	union power_supply_propval val = {0};
	int ret;

	val.intval = 0;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_IN_HARD_RESET, &val);

	if (pd->current_pr == PR_SINK) {
		usbpd_set_state(pd, PE_SNK_STARTUP);
	} else if (pd->current_pr == PR_SRC) {
		if (!pd->vconn_enabled && pd->typec_mode ==
				QTI_POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE) {
			if (!pd->vconn) {
				pd->vconn = devm_regulator_get(
					pd->dev.parent, "vconn");
				if (IS_ERR(pd->vconn)) {
					usbpd_err(&pd->dev, "Unable to get vconn\n");
					return;
				}
			}
			ret = regulator_enable(pd->vconn);
			if (ret)
				usbpd_err(&pd->dev, "Unable to enable vconn\n");
			else
				pd->vconn_enabled = true;
		}
		enable_vbus(pd);

		usbpd_set_state(pd, PE_SRC_STARTUP);
	}
}

static void enter_state_error_recovery(struct usbpd *pd)
{
	/* perform hard disconnect/reconnect */
	pd->in_pr_swap = false;
	pd->current_pr = PR_NONE;
	set_power_role(pd, PR_NONE);
	pd->typec_mode = QTI_POWER_SUPPLY_TYPEC_NONE;
	kick_sm(pd, 0);
}

static void enter_state_src_disabled(struct usbpd *pd)
{
	/* are we still connected? */
	if (pd->typec_mode == QTI_POWER_SUPPLY_TYPEC_NONE) {
		pd->current_pr = PR_NONE;
		kick_sm(pd, 0);
	}
}

static int usbpd_startup_common(struct usbpd *pd,
	struct pd_phy_params *phy_params)
{
	int ret = 0;

	pd_reset_protocol(pd);

	if (!pd->in_pr_swap) {
		/*
		 * support up to PD 3.0; if peer is 2.0
		 * phy_msg_received() will handle the downgrade.
		 */
		pd->spec_rev = USBPD_REV_30;

		if (pd->pd_phy_opened) {
			pd->pdphy_ops->close();
			pd->pd_phy_opened = false;
		}

		phy_params->data_role = pd->current_dr;
		phy_params->power_role = pd->current_pr;

		if (pd->vconn_enabled)
			phy_params->frame_filter_val |= FRAME_FILTER_EN_SOPI;

		if (!pd->pdphy_ops->open)
			return -ENODEV;

		ret = pd->pdphy_ops->open(phy_params);
		if (ret) {
			WARN_ON_ONCE(1);
			usbpd_err(&pd->dev, "error opening PD PHY %d\n",
					ret);
			pd->current_state = PE_UNKNOWN;
			return ret;
		}

		pd->pd_phy_opened = true;
	}

	return 0;
}

static void enter_state_src_startup(struct usbpd *pd)
{
	struct pd_phy_params phy_params = {
		.signal_cb		= phy_sig_received,
		.msg_rx_cb		= phy_msg_received,
		.shutdown_cb		= phy_shutdown,
		.frame_filter_val	= FRAME_FILTER_EN_SOP |
					  FRAME_FILTER_EN_HARD_RESET,
	};
	union power_supply_propval val = {0};

	if (pd->current_dr == DR_NONE) {
		pd->current_dr = DR_DFP;
		start_usb_host(pd, true);
		pd->ss_lane_svid = 0x0;
		typec_set_data_role(pd->typec_port, TYPEC_HOST);
	}

	typec_set_pwr_role(pd->typec_port, TYPEC_SOURCE);
	if (!pd->partner) {
		typec_set_pwr_opmode(pd->typec_port, TYPEC_PWR_MODE_1_5A);
		memset(&pd->partner_identity, 0, sizeof(pd->partner_identity));
		pd->partner_desc.usb_pd = false;
		pd->partner_desc.accessory = TYPEC_ACCESSORY_NONE;
		pd->partner = typec_register_partner(pd->typec_port,
				&pd->partner_desc);
	}

	val.intval = 1; /* Rp-1.5A; SinkTxNG for PD 3.0 */
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_SRC_RP, &val);

	/* Set CC back to DRP toggle for the next disconnect */
	val.intval = QTI_POWER_SUPPLY_TYPEC_PR_DUAL;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &val);

	if (usbpd_startup_common(pd, &phy_params))
		return;

	if (pd->in_pr_swap) {
		pd->in_pr_swap = false;
		val.intval = 0;
		usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PR_SWAP, &val);
	}

	if (pd->vconn_enabled) {
		/*
		 * wait for tVCONNStable (50ms), until SOPI becomes
		 * ready for communication.
		 */
		usleep_range(50000, 51000);
		usbpd_send_svdm(pd, USBPD_SID,
				USBPD_SVDM_DISCOVER_IDENTITY,
				SVDM_CMD_TYPE_INITIATOR, 0, NULL, 0);
		handle_vdm_tx(pd, SOPI_MSG);
		pd->current_state = PE_SRC_STARTUP_WAIT_FOR_VDM_RESP;
		kick_sm(pd, SENDER_RESPONSE_TIME);
		return;
	}
	/*
	 * A sink might remove its terminations (during some Type-C
	 * compliance tests or a sink attempting to do Try.SRC)
	 * at this point just after we enabled VBUS. Sending PD
	 * messages now would delay detecting the detach beyond the
	 * required timing. Instead, delay sending out the first
	 * source capabilities to allow for the other side to
	 * completely settle CC debounce and allow HW to detect detach
	 * sooner in the meantime. PD spec allows up to
	 * tFirstSourceCap (250ms).
	 */
	pd->current_state = PE_SRC_SEND_CAPABILITIES;
	kick_sm(pd, FIRST_SOURCE_CAP_TIME);
}

static void handle_state_src_startup(struct usbpd *pd, struct rx_msg *rx_msg)
{
	usbpd_set_state(pd, PE_SRC_STARTUP);
}

static void handle_state_src_startup_wait_for_vdm_resp(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	int ms;

	if (IS_DATA(rx_msg, MSG_VDM))
		handle_vdm_rx(pd, rx_msg);

	/* tVCONNStable (50ms) elapsed */
	ms = FIRST_SOURCE_CAP_TIME - 50;

	/* if no vdm msg received SENDER_RESPONSE_TIME elapsed */
	if (!rx_msg)
		ms -= SENDER_RESPONSE_TIME;

	/*
	 * Emarker may have negotiated down to rev 2.0.
	 * Reset to 3.0 to begin SOP communication with sink
	 */
	pd->spec_rev = USBPD_REV_30;

	pd->current_state = PE_SRC_SEND_CAPABILITIES;
	kick_sm(pd, ms);
}

static void enter_state_src_send_capabilities(struct usbpd *pd)
{
	kick_sm(pd, 0);
}

static void handle_state_src_send_capabilities(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	int ret;
	union power_supply_propval val = {0};

	ret = pd_send_msg(pd, MSG_SOURCE_CAPABILITIES, default_src_caps,
			ARRAY_SIZE(default_src_caps), SOP_MSG);
	if (ret) {
		if (pd->pd_connected) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}
		/*
		 * Technically this is PE_SRC_Discovery, but we can
		 * handle it by setting a timer to come back to the
		 * same state for the next retry.
		 */
		pd->caps_count++;
		if (pd->caps_count >= PD_CAPS_COUNT) {
			usbpd_dbg(&pd->dev, "Src CapsCounter exceeded, disabling PD\n");
			usbpd_set_state(pd, PE_SRC_DISABLED);

			val.intval = QTI_POWER_SUPPLY_PD_INACTIVE;
			usbpd_set_psy_iio_property(pd,
					POWER_SUPPLY_PROP_PD_ACTIVE, &val);
			return;
		}

		kick_sm(pd, SRC_CAP_TIME);
		return;
	}

	/* transmit was successful if GoodCRC was received */
	pd->caps_count = 0;
	pd->hard_reset_count = 0;
	pd->pd_connected = true; /* we know peer is PD capable */

	/* wait for REQUEST */
	pd->current_state = PE_SRC_SEND_CAPABILITIES_WAIT;
	kick_sm(pd, SENDER_RESPONSE_TIME);

	val.intval = QTI_POWER_SUPPLY_PD_ACTIVE;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_ACTIVE, &val);
}

static void handle_state_src_send_capabilities_wait(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (IS_DATA(rx_msg, MSG_REQUEST)) {
		pd->rdo = *(u32 *)rx_msg->payload;
		usbpd_set_state(pd, PE_SRC_NEGOTIATE_CAPABILITY);
	} else if (rx_msg) {
		usbpd_err(&pd->dev, "Unexpected message received\n");
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
	} else {
		usbpd_set_state(pd, PE_SRC_HARD_RESET);
	}
}

static void enter_state_src_negotiate_capability(struct usbpd *pd)
{
	int ret;

	log_decoded_request(pd, pd->rdo);
	pd->peer_usb_comm = PD_RDO_USB_COMM(pd->rdo);

	if (PD_RDO_OBJ_POS(pd->rdo) != 1 ||
			PD_RDO_FIXED_CURR(pd->rdo) >
				PD_SRC_PDO_FIXED_MAX_CURR(*default_src_caps)) {
		/* send Reject */
		ret = pd_send_msg(pd, MSG_REJECT, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		usbpd_err(&pd->dev, "Invalid request: %08x\n", pd->rdo);

		if (pd->in_explicit_contract)
			usbpd_set_state(pd, PE_SRC_READY);
		else
			/*
			 * bypass PE_SRC_Capability_Response and
			 * PE_SRC_Wait_New_Capabilities in this
			 * implementation for simplicity.
			 */
			usbpd_set_state(pd, PE_SRC_SEND_CAPABILITIES);
		return;
	}

	/* PE_SRC_TRANSITION_SUPPLY pseudo-state */
	ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
	if (ret) {
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		return;
	}

	/* tSrcTransition required after ACCEPT */
	usleep_range(SRC_TRANSITION_TIME * USEC_PER_MSEC,
			(SRC_TRANSITION_TIME + 5) * USEC_PER_MSEC);

	/*
	 * Normally a voltage change should occur within tSrcReady
	 * but since we only support VSafe5V there is nothing more to
	 * prepare from the power supply so send PS_RDY right away.
	 */
	ret = pd_send_msg(pd, MSG_PS_RDY, NULL, 0, SOP_MSG);
	if (ret) {
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		return;
	}

	usbpd_set_state(pd, PE_SRC_READY);
}

static void enter_state_src_ready(struct usbpd *pd)
{
	/*
	 * USB Host stack was started at PE_SRC_STARTUP but if peer
	 * doesn't support USB communication, we can turn it off
	 */
	if (pd->current_dr == DR_DFP && !pd->peer_usb_comm &&
			!pd->in_explicit_contract)
		stop_usb_host(pd);

	pd->in_explicit_contract = true;

	if (pd->vdm_tx && !pd->sm_queued)
		kick_sm(pd, 0);
	else if (pd->current_dr == DR_DFP && pd->vdm_state == VDM_NONE)
		usbpd_send_svdm(pd, USBPD_SID,
				USBPD_SVDM_DISCOVER_IDENTITY,
				SVDM_CMD_TYPE_INITIATOR, 0, NULL, 0);
	else
		start_src_ams(pd, false);

	kobject_uevent(&pd->dev.kobj, KOBJ_CHANGE);
	complete(&pd->is_ready);
	typec_set_pwr_opmode(pd->typec_port, TYPEC_PWR_MODE_PD);
}

static void handle_state_src_ready(struct usbpd *pd, struct rx_msg *rx_msg)
{
	int ret;

	if (IS_CTRL(rx_msg, MSG_GET_SOURCE_CAP)) {
		pd->current_state = PE_SRC_SEND_CAPABILITIES;
		kick_sm(pd, 0);
	} else if (IS_CTRL(rx_msg, MSG_GET_SINK_CAP)) {
		ret = pd_send_msg(pd, MSG_SINK_CAPABILITIES,
				pd->sink_caps, pd->num_sink_caps,
				SOP_MSG);
		if (ret)
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
	} else if (IS_DATA(rx_msg, MSG_REQUEST)) {
		pd->rdo = *(u32 *)rx_msg->payload;
		usbpd_set_state(pd, PE_SRC_NEGOTIATE_CAPABILITY);
	} else if (IS_CTRL(rx_msg, MSG_DR_SWAP)) {
		if (pd->vdm_state == MODE_ENTERED) {
			usbpd_set_state(pd, PE_SRC_HARD_RESET);
			return;
		}

		ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		dr_swap(pd);
	} else if (IS_CTRL(rx_msg, MSG_PR_SWAP)) {
		/* we'll happily accept Src->Sink requests anytime */
		ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		usbpd_set_state(pd, PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
		return;
	} else if (IS_CTRL(rx_msg, MSG_VCONN_SWAP)) {
		ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		vconn_swap(pd);
	} else if (IS_DATA(rx_msg, MSG_VDM)) {
		handle_vdm_rx(pd, rx_msg);
	} else if (IS_CTRL(rx_msg, MSG_GET_SOURCE_CAP_EXTENDED)) {
		handle_get_src_cap_extended(pd);
	} else if (IS_EXT(rx_msg, MSG_GET_BATTERY_CAP)) {
		handle_get_battery_cap(pd, rx_msg);
	} else if (IS_EXT(rx_msg, MSG_GET_BATTERY_STATUS)) {
		handle_get_battery_status(pd, rx_msg);
	} else if (IS_CTRL(rx_msg, MSG_ACCEPT) ||
		   IS_CTRL(rx_msg, MSG_REJECT) ||
		   IS_CTRL(rx_msg, MSG_WAIT)) {
		usbpd_warn(&pd->dev, "Unexpected message\n");
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		return;
	} else if (rx_msg && !IS_CTRL(rx_msg, MSG_NOT_SUPPORTED)) {
		usbpd_dbg(&pd->dev, "Unsupported message\n");
		ret = pd_send_msg(pd, pd->spec_rev == USBPD_REV_30 ?
				MSG_NOT_SUPPORTED : MSG_REJECT,
				NULL, 0, SOP_MSG);
		if (ret)
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		return;
	} else if (pd->send_pr_swap) {
		pd->send_pr_swap = false;
		ret = pd_send_msg(pd, MSG_PR_SWAP, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		pd->current_state = PE_PRS_SRC_SNK_SEND_SWAP;
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->send_dr_swap) {
		pd->send_dr_swap = false;
		ret = pd_send_msg(pd, MSG_DR_SWAP, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		pd->current_state = PE_DRS_SEND_DR_SWAP;
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->vdm_tx) {
		handle_vdm_tx(pd, SOP_MSG);
	} else {
		start_src_ams(pd, false);
	}
}

static void enter_state_hard_reset(struct usbpd *pd)
{
	union power_supply_propval val = {0};

	/* are we still connected? */
	if (pd->typec_mode == QTI_POWER_SUPPLY_TYPEC_NONE) {
		pd->current_pr = PR_NONE;
		kick_sm(pd, 0);
		return;
	}

	val.intval = 1;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_IN_HARD_RESET, &val);

	if (pd->current_pr == PR_SINK) {
		if (pd->requested_current) {
			val.intval = pd->requested_current = 0;
			usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_CURRENT_MAX, &val);
		}

		val.intval = pd->requested_voltage = 5000000;
		usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_VOLTAGE_MIN, &val);
	}

	pd_send_hard_reset(pd);
	pd->in_explicit_contract = false;
	pd->rdo = 0;
	rx_msg_cleanup(pd);
	reset_vdm_state(pd);
	kobject_uevent(&pd->dev.kobj, KOBJ_CHANGE);

	if (pd->current_pr == PR_SRC) {
		pd->current_state = PE_SRC_TRANSITION_TO_DEFAULT;
		kick_sm(pd, PS_HARD_RESET_TIME);
	} else {
		usbpd_set_state(pd, PE_SNK_TRANSITION_TO_DEFAULT);
	}
}

static void handle_state_soft_reset(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	int ret;

	pd_reset_protocol(pd);

	ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
	if (ret) {
		usbpd_set_state(pd, pd->current_pr == PR_SRC ?
				PE_SRC_HARD_RESET : PE_SNK_HARD_RESET);
		return;
	}

	usbpd_set_state(pd, pd->current_pr == PR_SRC ?
			PE_SRC_SEND_CAPABILITIES :
			PE_SNK_WAIT_FOR_CAPABILITIES);
}

static void handle_state_src_transition_to_default(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (pd->vconn_enabled)
		regulator_disable(pd->vconn);
	pd->vconn_enabled = false;

	if (pd->vbus_enabled)
		regulator_disable(pd->vbus);
	pd->vbus_enabled = false;

	if (pd->current_dr != DR_DFP) {
		extcon_set_state_sync(pd->extcon, EXTCON_USB, 0);
		pd->current_dr = DR_DFP;
		pd->pdphy_ops->update_roles(pd->current_dr, pd->current_pr);
	}

	/* PE_UNKNOWN will turn on VBUS and go back to PE_SRC_STARTUP */
	pd->current_state = PE_UNKNOWN;
	kick_sm(pd, SRC_RECOVER_TIME);
}

static void enter_state_snk_startup(struct usbpd *pd)
{
	int ret;
	struct pd_phy_params phy_params = {
		.signal_cb		= phy_sig_received,
		.msg_rx_cb		= phy_msg_received,
		.shutdown_cb		= phy_shutdown,
		.frame_filter_val	= FRAME_FILTER_EN_SOP |
					  FRAME_FILTER_EN_HARD_RESET,
	};
	union power_supply_propval val = {0};

	if (pd->current_dr == DR_NONE || pd->current_dr == DR_UFP) {
		pd->current_dr = DR_UFP;

		ret = usbpd_get_psy_iio_property(pd,
				POWER_SUPPLY_PROP_REAL_TYPE, &val);
		if (!ret) {
			usbpd_dbg(&pd->dev, "type:%d\n", val.intval);
			if (val.intval == POWER_SUPPLY_TYPE_USB ||
				val.intval == POWER_SUPPLY_TYPE_USB_CDP)
				start_usb_peripheral(pd);
		}
		typec_set_data_role(pd->typec_port, TYPEC_DEVICE);
	}

	typec_set_pwr_role(pd->typec_port, TYPEC_SINK);
	if (!pd->partner) {
		typec_set_pwr_opmode(pd->typec_port,
			pd->typec_mode - QTI_POWER_SUPPLY_TYPEC_SOURCE_DEFAULT);
		memset(&pd->partner_identity, 0, sizeof(pd->partner_identity));
		pd->partner_desc.usb_pd = false;
		pd->partner_desc.accessory = TYPEC_ACCESSORY_NONE;
		pd->partner = typec_register_partner(pd->typec_port,
				&pd->partner_desc);
	}

	if (usbpd_startup_common(pd, &phy_params))
		return;

	pd->current_voltage = pd->requested_voltage = 5000000;
	val.intval = pd->requested_voltage; /* set max range to 5V */
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_VOLTAGE_MAX, &val);

	if (!pd->vbus_present) {
		pd->current_state = PE_SNK_DISCOVERY;
		/* max time for hard reset to turn vbus back on */
		kick_sm(pd, SNK_HARD_RESET_VBUS_ON_TIME);
		return;
	}

	usbpd_set_state(pd, PE_SNK_WAIT_FOR_CAPABILITIES);
}

static void handle_state_snk_startup(struct usbpd *pd, struct rx_msg *rx_msg)
{
	usbpd_set_state(pd, PE_SNK_STARTUP);
}

static void handle_state_snk_discovery(struct usbpd *pd, struct rx_msg *rx_msg)
{
	if (!rx_msg) {
		if (pd->vbus_present)
			usbpd_set_state(pd,
					PE_SNK_WAIT_FOR_CAPABILITIES);

		/*
		 * Handle disconnection in the middle of PR_Swap.
		 * Since in psy_changed() if pd->in_pr_swap is true
		 * we ignore the typec_mode==NONE change since that is
		 * expected to happen. However if the cable really did
		 * get disconnected we need to check for it here after
		 * waiting for VBUS presence times out.
		 */
		if (!pd->typec_mode) {
			pd->current_pr = PR_NONE;
			kick_sm(pd, 0);
		}

		return;
	}

	pd->current_state = PE_SNK_WAIT_FOR_CAPABILITIES;
	handle_state_snk_wait_for_capabilities(pd, rx_msg);
}

static void enter_state_snk_wait_for_capabilities(struct usbpd *pd)
{
	unsigned long flags;

	spin_lock_irqsave(&pd->rx_lock, flags);
	if (list_empty(&pd->rx_q))
		kick_sm(pd, SINK_WAIT_CAP_TIME);
	spin_unlock_irqrestore(&pd->rx_lock, flags);
}

static void handle_state_snk_wait_for_capabilities(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	union power_supply_propval val = {0};

	pd->in_pr_swap = false;
	val.intval = 0;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PR_SWAP, &val);

	if (IS_DATA(rx_msg, MSG_SOURCE_CAPABILITIES)) {
		val.intval = 0;
		usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_IN_HARD_RESET, &val);

		/* save the PDOs so userspace can further evaluate */
		memset(&pd->received_pdos, 0,
				sizeof(pd->received_pdos));
		memcpy(&pd->received_pdos, rx_msg->payload,
				min_t(size_t, rx_msg->data_len,
					sizeof(pd->received_pdos)));
		pd->src_cap_id++;

		usbpd_set_state(pd, PE_SNK_EVALUATE_CAPABILITY);
	} else if (pd->hard_reset_count < 3) {
		usbpd_set_state(pd, PE_SNK_HARD_RESET);
	} else {
		usbpd_dbg(&pd->dev, "Sink hard reset count exceeded, disabling PD\n");

		val.intval = 0;
		usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_IN_HARD_RESET, &val);

		val.intval = QTI_POWER_SUPPLY_PD_INACTIVE;
		usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_ACTIVE, &val);
	}
}

static void enter_state_snk_evaluate_capability(struct usbpd *pd)
{
	int ret;

	pd->hard_reset_count = 0;

	/* evaluate PDOs and select one */
	ret = pd_eval_src_caps(pd);
	if (ret < 0) {
		usbpd_err(&pd->dev, "Invalid src_caps received. Skipping request\n");
		return;
	}

	pd->pd_connected = true; /* we know peer is PD capable */
	usbpd_set_state(pd, PE_SNK_SELECT_CAPABILITY);
}

static void enter_state_snk_select_capability(struct usbpd *pd)
{
	int ret;

	log_decoded_request(pd, pd->rdo);

	ret = pd_send_msg(pd, MSG_REQUEST, &pd->rdo, 1, SOP_MSG);
	if (ret) {
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		return;
	}

	/* wait for ACCEPT */
	kick_sm(pd, SENDER_RESPONSE_TIME);
}

static void handle_state_snk_select_capability(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	int ret;
	union power_supply_propval val = {0};

	if (IS_CTRL(rx_msg, MSG_ACCEPT)) {
		u32 pdo = pd->received_pdos[pd->requested_pdo - 1];
		bool same_pps = (pd->selected_pdo == pd->requested_pdo)
			&& (PD_SRC_PDO_TYPE(pdo) ==
					PD_SRC_PDO_TYPE_AUGMENTED);

		usbpd_set_state(pd, PE_SNK_TRANSITION_SINK);

		/* prepare for voltage increase/decrease */
		val.intval = pd->requested_voltage;
		usbpd_set_psy_iio_property(pd,
			pd->requested_voltage >= pd->current_voltage ?
				POWER_SUPPLY_PROP_PD_VOLTAGE_MAX :
				POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
				&val);

		/*
		 * if changing voltages (not within the same PPS PDO),
		 * we must lower input current to pSnkStdby (2.5W).
		 * Calculate it and set PD_CURRENT_MAX accordingly.
		 */
		if (!same_pps &&
			pd->requested_voltage != pd->current_voltage) {
			int mv = max(pd->requested_voltage,
					pd->current_voltage) / 1000;
			val.intval = (2500000 / mv) * 1000;
			usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_CURRENT_MAX, &val);
		} else {
			/* decreasing current? */
			ret = usbpd_get_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_CURRENT_MAX, &val);
			if (!ret &&
				pd->requested_current < val.intval) {
				val.intval =
					pd->requested_current * 1000;
				usbpd_set_psy_iio_property(pd,
					POWER_SUPPLY_PROP_PD_CURRENT_MAX,
					&val);
			}
		}

		pd->selected_pdo = pd->requested_pdo;
	} else if (IS_CTRL(rx_msg, MSG_REJECT) ||
			IS_CTRL(rx_msg, MSG_WAIT)) {
		if (pd->in_explicit_contract)
			usbpd_set_state(pd, PE_SNK_READY);
		else
			usbpd_set_state(pd,
					PE_SNK_WAIT_FOR_CAPABILITIES);
	} else if (rx_msg) {
		usbpd_err(&pd->dev, "Invalid response to sink request\n");
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
	} else {
		/* timed out; go to hard reset */
		usbpd_set_state(pd, PE_SNK_HARD_RESET);
	}
}

static void enter_state_snk_transition_sink(struct usbpd *pd)
{
	/* wait for PS_RDY */
	kick_sm(pd, PS_TRANSITION_TIME);
}

static void handle_state_snk_transition_sink(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	union power_supply_propval val = {0};

	if (IS_CTRL(rx_msg, MSG_PS_RDY)) {
		val.intval = pd->requested_voltage;
		usbpd_set_psy_iio_property(pd,
			pd->requested_voltage >= pd->current_voltage ?
				POWER_SUPPLY_PROP_PD_VOLTAGE_MIN :
				POWER_SUPPLY_PROP_PD_VOLTAGE_MAX, &val);
		pd->current_voltage = pd->requested_voltage;

		/* resume charging */
		val.intval = pd->requested_current * 1000; /* mA->uA */
		usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_CURRENT_MAX, &val);

		usbpd_set_state(pd, PE_SNK_READY);
	} else {
		/* timed out; go to hard reset */
		usbpd_set_state(pd, PE_SNK_HARD_RESET);
	}
}

static void enter_state_snk_ready(struct usbpd *pd)
{
	pd->in_explicit_contract = true;

	if (pd->vdm_tx)
		kick_sm(pd, 0);
	else if (pd->current_dr == DR_DFP && pd->vdm_state == VDM_NONE)
		usbpd_send_svdm(pd, USBPD_SID,
				USBPD_SVDM_DISCOVER_IDENTITY,
				SVDM_CMD_TYPE_INITIATOR, 0, NULL, 0);

	kobject_uevent(&pd->dev.kobj, KOBJ_CHANGE);
	complete(&pd->is_ready);
	typec_set_pwr_opmode(pd->typec_port, TYPEC_PWR_MODE_PD);
}

static bool handle_ctrl_snk_ready(struct usbpd *pd, struct rx_msg *rx_msg)
{
	int ret;

	switch (PD_MSG_HDR_TYPE(rx_msg->hdr)) {
	case MSG_GET_SINK_CAP:
		ret = pd_send_msg(pd, MSG_SINK_CAPABILITIES,
				pd->sink_caps, pd->num_sink_caps,
				SOP_MSG);
		if (ret)
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		break;
	case MSG_GET_SOURCE_CAP:
		ret = pd_send_msg(pd, MSG_SOURCE_CAPABILITIES,
				default_src_caps,
				ARRAY_SIZE(default_src_caps), SOP_MSG);
		if (ret)
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		break;
	case MSG_DR_SWAP:
		if (pd->vdm_state == MODE_ENTERED) {
			usbpd_set_state(pd, PE_SNK_HARD_RESET);
			break;
		}

		ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			break;
		}

		dr_swap(pd);
		break;
	case MSG_PR_SWAP:
		/* TODO: should we Reject in certain circumstances? */
		ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			break;
		}

		usbpd_set_state(pd, PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
		break;
	case MSG_VCONN_SWAP:
		ret = pd_send_msg(pd, MSG_ACCEPT, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			break;
		}
		vconn_swap(pd);
		break;
	case MSG_GET_SOURCE_CAP_EXTENDED:
		handle_get_src_cap_extended(pd);
		break;
	case MSG_ACCEPT:
	case MSG_REJECT:
	case MSG_WAIT:
		usbpd_warn(&pd->dev, "Unexpected message\n");
		usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		break;
	default:
		return false;
	}

	return true;
}

static bool handle_data_snk_ready(struct usbpd *pd, struct rx_msg *rx_msg)
{
	u32 ado;

	switch (PD_MSG_HDR_TYPE(rx_msg->hdr)) {
	case MSG_SOURCE_CAPABILITIES:
		/* save the PDOs so userspace can further evaluate */
		memset(&pd->received_pdos, 0,
				sizeof(pd->received_pdos));
		memcpy(&pd->received_pdos, rx_msg->payload,
				min_t(size_t, rx_msg->data_len,
					sizeof(pd->received_pdos)));
		pd->src_cap_id++;

		usbpd_set_state(pd, PE_SNK_EVALUATE_CAPABILITY);
		break;
	case MSG_VDM:
		handle_vdm_rx(pd, rx_msg);
		break;
	case MSG_ALERT:
		if (rx_msg->data_len != sizeof(ado)) {
			usbpd_err(&pd->dev, "Invalid ado\n");
			break;
		}
		memcpy(&ado, rx_msg->payload, sizeof(ado));
		usbpd_dbg(&pd->dev, "Received Alert 0x%08x\n", ado);

		/*
		 * Don't send Get_Status right away so we can coalesce
		 * multiple Alerts. 150ms should be enough to not get
		 * in the way of any other AMS that might happen.
		 */
		pd->send_get_status = true;
		kick_sm(pd, 150);
		break;
	case MSG_BATTERY_STATUS:
		if (rx_msg->data_len != sizeof(pd->battery_sts_dobj)) {
			usbpd_err(&pd->dev, "Invalid bat sts dobj\n");
			break;
		}
		memcpy(&pd->battery_sts_dobj, rx_msg->payload,
			sizeof(pd->battery_sts_dobj));
		complete(&pd->is_ready);
		break;
	default:
		return false;
	}

	return true;
}

static bool handle_ext_snk_ready(struct usbpd *pd, struct rx_msg *rx_msg)
{
	switch (PD_MSG_HDR_TYPE(rx_msg->hdr)) {
	case MSG_SOURCE_CAPABILITIES_EXTENDED:
		if (rx_msg->data_len != PD_SRC_CAP_EXT_DB_LEN) {
			usbpd_err(&pd->dev, "Invalid src cap ext db\n");
			break;
		}
		memcpy(&pd->src_cap_ext_db, rx_msg->payload,
			sizeof(pd->src_cap_ext_db));
		complete(&pd->is_ready);
		break;
	case MSG_PPS_STATUS:
		if (rx_msg->data_len != sizeof(pd->pps_status_db)) {
			usbpd_err(&pd->dev, "Invalid pps status db\n");
			break;
		}
		memcpy(&pd->pps_status_db, rx_msg->payload,
			sizeof(pd->pps_status_db));
		complete(&pd->is_ready);
		break;
	case MSG_STATUS:
		if (rx_msg->data_len > PD_STATUS_DB_LEN)
			usbpd_err(&pd->dev, "Invalid status db length:%d\n",
					rx_msg->data_len);

		memset(&pd->status_db, 0, sizeof(pd->status_db));
		memcpy(&pd->status_db, rx_msg->payload,
			min((size_t)rx_msg->data_len, sizeof(pd->status_db)));
		kobject_uevent(&pd->dev.kobj, KOBJ_CHANGE);
		complete(&pd->is_ready);
		break;
	case MSG_BATTERY_CAPABILITIES:
		if (rx_msg->data_len != PD_BATTERY_CAP_DB_LEN) {
			usbpd_err(&pd->dev, "Invalid battery cap db\n");
			break;
		}
		memcpy(&pd->battery_cap_db, rx_msg->payload,
			sizeof(pd->battery_cap_db));
		complete(&pd->is_ready);
		break;
	case MSG_GET_BATTERY_CAP:
		handle_get_battery_cap(pd, rx_msg);
		break;
	case MSG_GET_BATTERY_STATUS:
		handle_get_battery_status(pd, rx_msg);
		break;
	default:
		return false;
	}

	return true;
}

static void handle_snk_ready_tx(struct usbpd *pd, struct rx_msg *rx_msg)
{
	int ret;

	if (pd->send_get_src_cap_ext) {
		pd->send_get_src_cap_ext = false;
		ret = pd_send_msg(pd, MSG_GET_SOURCE_CAP_EXTENDED, NULL,
			0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->send_get_pps_status) {
		pd->send_get_pps_status = false;
		ret = pd_send_msg(pd, MSG_GET_PPS_STATUS, NULL,
			0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->send_get_status) {
		pd->send_get_status = false;
		ret = pd_send_msg(pd, MSG_GET_STATUS, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->send_get_battery_cap) {
		pd->send_get_battery_cap = false;
		ret = pd_send_ext_msg(pd, MSG_GET_BATTERY_CAP,
			&pd->get_battery_cap_db, 1, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->send_get_battery_status) {
		pd->send_get_battery_status = false;
		ret = pd_send_ext_msg(pd, MSG_GET_BATTERY_STATUS,
			&pd->get_battery_status_db, 1, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->send_request) {
		pd->send_request = false;
		usbpd_set_state(pd, PE_SNK_SELECT_CAPABILITY);
	} else if (pd->send_pr_swap) {
		pd->send_pr_swap = false;
		ret = pd_send_msg(pd, MSG_PR_SWAP, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		pd->current_state = PE_PRS_SNK_SRC_SEND_SWAP;
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else if (pd->send_dr_swap) {
		pd->send_dr_swap = false;
		ret = pd_send_msg(pd, MSG_DR_SWAP, NULL, 0, SOP_MSG);
		if (ret) {
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
			return;
		}

		pd->current_state = PE_DRS_SEND_DR_SWAP;
		kick_sm(pd, SENDER_RESPONSE_TIME);
	} else {
		handle_vdm_tx(pd, SOP_MSG);
	}
}

static void handle_state_snk_ready(struct usbpd *pd, struct rx_msg *rx_msg)
{
	int ret;

	if (rx_msg && !PD_MSG_HDR_COUNT(rx_msg->hdr) &&
		handle_ctrl_snk_ready(pd, rx_msg)) {
		return;
	} else if (rx_msg && !PD_MSG_HDR_IS_EXTENDED(rx_msg->hdr) &&
		handle_data_snk_ready(pd, rx_msg)) {
		return;
	} else if (rx_msg && PD_MSG_HDR_IS_EXTENDED(rx_msg->hdr) &&
		handle_ext_snk_ready(pd, rx_msg)) {
		/* continue to handle tx */
	} else if (rx_msg && !IS_CTRL(rx_msg, MSG_NOT_SUPPORTED)) {
		usbpd_dbg(&pd->dev, "Unsupported message\n");
		ret = pd_send_msg(pd, pd->spec_rev == USBPD_REV_30 ?
				MSG_NOT_SUPPORTED : MSG_REJECT,
				NULL, 0, SOP_MSG);
		if (ret)
			usbpd_set_state(pd, PE_SEND_SOFT_RESET);
		return;
	}

	if (is_sink_tx_ok(pd))
		handle_snk_ready_tx(pd, rx_msg);
}

static void enter_state_snk_transition_to_default(struct usbpd *pd)
{
	if (pd->current_dr != DR_UFP) {
		stop_usb_host(pd);
		start_usb_peripheral(pd);
		pd->current_dr = DR_UFP;
		pd->pdphy_ops->update_roles(pd->current_dr, pd->current_pr);
	}
	if (pd->vconn_enabled) {
		regulator_disable(pd->vconn);
		pd->vconn_enabled = false;
	}

	/* max time for hard reset to turn vbus off */
	kick_sm(pd, SNK_HARD_RESET_VBUS_OFF_TIME);
}

static void handle_state_snk_transition_to_default(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	usbpd_set_state(pd, PE_SNK_STARTUP);
}

static void handle_state_drs_send_dr_swap(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (IS_CTRL(rx_msg, MSG_ACCEPT))
		dr_swap(pd);

	usbpd_set_state(pd, pd->current_pr == PR_SRC ?
			PE_SRC_READY : PE_SNK_READY);
}

static void handle_state_prs_snk_src_send_swap(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (!IS_CTRL(rx_msg, MSG_ACCEPT)) {
		pd->current_state = PE_SNK_READY;
		return;
	}

	usbpd_set_state(pd, PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
}

static void enter_state_prs_snk_src_transition_to_off(struct usbpd *pd)
{
	union power_supply_propval val = {0};

	val.intval = pd->in_pr_swap = true;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PR_SWAP, &val);

	/* lock in current mode */
	set_power_role(pd, pd->current_pr);

	val.intval = pd->requested_current = 0; /* suspend charging */
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_CURRENT_MAX, &val);

	pd->in_explicit_contract = false;

	/*
	 * need to update PR bit in message header so that
	 * proper GoodCRC is sent when receiving next PS_RDY
	 */
	pd->pdphy_ops->update_roles(pd->current_dr, PR_SRC);

	/* wait for PS_RDY */
	kick_sm(pd, PS_SOURCE_OFF);
}

static void handle_state_prs_snk_src_transition_to_off(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (!IS_CTRL(rx_msg, MSG_PS_RDY)) {
		usbpd_set_state(pd, PE_ERROR_RECOVERY);
		return;
	}

	/* PE_PRS_SNK_SRC_Assert_Rp */
	pd->current_pr = PR_SRC;
	set_power_role(pd, pd->current_pr);
	pd->current_state = PE_PRS_SNK_SRC_SOURCE_ON;

	handle_state_prs_snk_src_source_on(pd, rx_msg);
}

static void handle_state_prs_snk_src_source_on(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	int ret;

	enable_vbus(pd);

	ret = pd_send_msg(pd, MSG_PS_RDY, NULL, 0, SOP_MSG);
	if (ret) {
		usbpd_set_state(pd, PE_ERROR_RECOVERY);
		return;
	}

	usbpd_set_state(pd, PE_SRC_STARTUP);
}

static void handle_state_prs_src_snk_send_swap(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (!IS_CTRL(rx_msg, MSG_ACCEPT)) {
		pd->current_state = PE_SRC_READY;
		return;
	}

	usbpd_set_state(pd, PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
}

static void enter_state_prs_src_snk_transition_to_off(struct usbpd *pd)
{
	union power_supply_propval val = {0};

	val.intval = pd->in_pr_swap = true;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PR_SWAP, &val);
	pd->in_explicit_contract = false;

	/* lock in current mode */
	set_power_role(pd, pd->current_pr);

	kick_sm(pd, SRC_TRANSITION_TIME);
}

static void handle_state_prs_src_snk_transition_to_off(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	int ret;

	if (pd->vbus_enabled) {
		regulator_disable(pd->vbus);
		pd->vbus_enabled = false;
	}

	/* PE_PRS_SRC_SNK_Assert_Rd */
	pd->current_pr = PR_SINK;
	set_power_role(pd, pd->current_pr);
	pd->pdphy_ops->update_roles(pd->current_dr, pd->current_pr);

	/* allow time for Vbus discharge, must be < tSrcSwapStdby */
	msleep(500);

	ret = pd_send_msg(pd, MSG_PS_RDY, NULL, 0, SOP_MSG);
	if (ret) {
		usbpd_set_state(pd, PE_ERROR_RECOVERY);
		return;
	}

	pd->current_state = PE_PRS_SRC_SNK_WAIT_SOURCE_ON;
	kick_sm(pd, PS_SOURCE_ON);
}

static void handle_state_prs_src_snk_wait_source_on(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (IS_CTRL(rx_msg, MSG_PS_RDY))
		usbpd_set_state(pd, PE_SNK_STARTUP);
	else
		usbpd_set_state(pd, PE_ERROR_RECOVERY);
}

static void enter_state_send_soft_reset(struct usbpd *pd)
{
	int ret;

	pd_reset_protocol(pd);

	ret = pd_send_msg(pd, MSG_SOFT_RESET, NULL, 0, SOP_MSG);
	if (ret) {
		usbpd_set_state(pd, pd->current_pr == PR_SRC ?
				PE_SRC_HARD_RESET : PE_SNK_HARD_RESET);
		return;
	}

	/* wait for ACCEPT */
	kick_sm(pd, SENDER_RESPONSE_TIME);
}

static void handle_state_send_soft_reset(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (IS_CTRL(rx_msg, MSG_ACCEPT)) {
		usbpd_set_state(pd, pd->current_pr == PR_SRC ?
				PE_SRC_SEND_CAPABILITIES :
				PE_SNK_WAIT_FOR_CAPABILITIES);
	} else {
		usbpd_err(&pd->dev, "%s: Did not see Accept, do Hard Reset\n",
				usbpd_state_strings[pd->current_state]);
		usbpd_set_state(pd, pd->current_pr == PR_SRC ?
				PE_SRC_HARD_RESET : PE_SNK_HARD_RESET);
	}
}

static void handle_state_vcs_wait_for_vconn(struct usbpd *pd,
	struct rx_msg *rx_msg)
{
	if (IS_CTRL(rx_msg, MSG_PS_RDY)) {
		/*
		 * hopefully redundant check but in case not enabled
		 * avoids unbalanced regulator disable count
		 */
		if (pd->vconn_enabled)
			regulator_disable(pd->vconn);
		pd->vconn_enabled = false;

		pd->current_state = pd->current_pr == PR_SRC ?
			PE_SRC_READY : PE_SNK_READY;
	} else {
		/* timed out; go to hard reset */
		usbpd_set_state(pd, pd->current_pr == PR_SRC ?
				PE_SRC_HARD_RESET : PE_SNK_HARD_RESET);
	}
}

/* Enters new state and executes actions on entry */
static void usbpd_set_state(struct usbpd *pd, enum usbpd_state next_state)
{
	if (pd->hard_reset_recvd) /* let usbpd_sm handle it */
		return;

	usbpd_dbg(&pd->dev, "%s -> %s\n",
			usbpd_state_strings[pd->current_state],
			usbpd_state_strings[next_state]);

	pd->current_state = next_state;

	if (pd->current_state < PE_MAX_STATES &&
		state_handlers[pd->current_state].enter_state)
		state_handlers[pd->current_state].enter_state(pd);
	else
		usbpd_dbg(&pd->dev, "No action for state %s\n",
				usbpd_state_strings[pd->current_state]);
}

static const struct usbpd_state_handler state_handlers[] = {
	[PE_UNKNOWN] = {NULL, handle_state_unknown},
	[PE_ERROR_RECOVERY] = {enter_state_error_recovery, NULL},
	[PE_SRC_DISABLED] = {enter_state_src_disabled, NULL},
	[PE_SRC_STARTUP] = {enter_state_src_startup, handle_state_src_startup},
	[PE_SRC_STARTUP_WAIT_FOR_VDM_RESP] = {NULL,
				handle_state_src_startup_wait_for_vdm_resp},
	[PE_SRC_SEND_CAPABILITIES] = {enter_state_src_send_capabilities,
				handle_state_src_send_capabilities},
	[PE_SRC_SEND_CAPABILITIES_WAIT] = {NULL,
				handle_state_src_send_capabilities_wait},
	[PE_SRC_NEGOTIATE_CAPABILITY] = {enter_state_src_negotiate_capability,
					NULL},
	[PE_SRC_READY] = {enter_state_src_ready, handle_state_src_ready},
	[PE_SRC_HARD_RESET] = {enter_state_hard_reset, NULL},
	[PE_SRC_SOFT_RESET] = {NULL, handle_state_soft_reset},
	[PE_SRC_TRANSITION_TO_DEFAULT] = {NULL,
				handle_state_src_transition_to_default},
	[PE_SNK_STARTUP] = {enter_state_snk_startup,
				handle_state_snk_startup},
	[PE_SNK_DISCOVERY] = {NULL, handle_state_snk_discovery},
	[PE_SNK_WAIT_FOR_CAPABILITIES] = {
					enter_state_snk_wait_for_capabilities,
					handle_state_snk_wait_for_capabilities},
	[PE_SNK_EVALUATE_CAPABILITY] = {enter_state_snk_evaluate_capability,
					NULL},
	[PE_SNK_SELECT_CAPABILITY] = {enter_state_snk_select_capability,
					handle_state_snk_select_capability},
	[PE_SNK_TRANSITION_SINK] = {enter_state_snk_transition_sink,
					handle_state_snk_transition_sink},
	[PE_SNK_READY] = {enter_state_snk_ready, handle_state_snk_ready},
	[PE_SNK_HARD_RESET] = {enter_state_hard_reset, NULL},
	[PE_SNK_SOFT_RESET] = {NULL, handle_state_soft_reset},
	[PE_SNK_TRANSITION_TO_DEFAULT] = {
				enter_state_snk_transition_to_default,
				handle_state_snk_transition_to_default},
	[PE_DRS_SEND_DR_SWAP] = {NULL, handle_state_drs_send_dr_swap},
	[PE_PRS_SNK_SRC_SEND_SWAP] = {NULL, handle_state_prs_snk_src_send_swap},
	[PE_PRS_SNK_SRC_TRANSITION_TO_OFF] = {
				enter_state_prs_snk_src_transition_to_off,
				handle_state_prs_snk_src_transition_to_off},
	[PE_PRS_SNK_SRC_SOURCE_ON] = {NULL, handle_state_prs_snk_src_source_on},
	[PE_PRS_SRC_SNK_SEND_SWAP] = {NULL, handle_state_prs_src_snk_send_swap},
	[PE_PRS_SRC_SNK_TRANSITION_TO_OFF] = {
				enter_state_prs_src_snk_transition_to_off,
				handle_state_prs_src_snk_transition_to_off},
	[PE_PRS_SRC_SNK_WAIT_SOURCE_ON] = {NULL,
			handle_state_prs_src_snk_wait_source_on},
	[PE_SEND_SOFT_RESET] = {enter_state_send_soft_reset,
				handle_state_send_soft_reset},
	[PE_VCS_WAIT_FOR_VCONN] = {NULL, handle_state_vcs_wait_for_vconn},
};

static void handle_disconnect(struct usbpd *pd)
{
	union power_supply_propval val = {0};

	if (pd->vconn_enabled) {
		regulator_disable(pd->vconn);
		pd->vconn_enabled = false;
	}

	usbpd_info(&pd->dev, "USB Type-C disconnect\n");

	if (pd->pd_phy_opened) {
		pd->pdphy_ops->close();
		pd->pd_phy_opened = false;
	}

	pd->in_pr_swap = false;
	pd->pd_connected = false;
	pd->in_explicit_contract = false;
	pd->hard_reset_recvd = false;
	pd->caps_count = 0;
	pd->hard_reset_count = 0;
	pd->requested_voltage = 0;
	pd->requested_current = 0;
	pd->selected_pdo = pd->requested_pdo = 0;
	pd->peer_usb_comm = pd->peer_pr_swap = pd->peer_dr_swap = false;
	memset(&pd->received_pdos, 0, sizeof(pd->received_pdos));
	rx_msg_cleanup(pd);

	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_IN_HARD_RESET, &val);

	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED, &val);

	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_ACTIVE, &val);

	if (pd->vbus_enabled) {
		regulator_disable(pd->vbus);
		pd->vbus_enabled = false;
	}

	reset_vdm_state(pd);
	if (pd->current_dr == DR_UFP)
		stop_usb_peripheral(pd);
	else if (pd->current_dr == DR_DFP)
		stop_usb_host(pd);

	pd->current_dr = DR_NONE;

	if (pd->current_state == PE_ERROR_RECOVERY)
		/* forced disconnect, wait before resetting to DRP */
		usleep_range(ERROR_RECOVERY_TIME * USEC_PER_MSEC,
			(ERROR_RECOVERY_TIME + 5) * USEC_PER_MSEC);

	val.intval = 0;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PR_SWAP, &val);

	/* set due to dual_role class "mode" change */
	if (pd->forced_pr != QTI_POWER_SUPPLY_TYPEC_PR_NONE)
		val.intval = pd->forced_pr;
	else
		/* Set CC back to DRP toggle */
		val.intval = QTI_POWER_SUPPLY_TYPEC_PR_DUAL;

	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &val);
	pd->forced_pr = QTI_POWER_SUPPLY_TYPEC_PR_NONE;

	pd->current_state = PE_UNKNOWN;
	pd_reset_protocol(pd);

	kobject_uevent(&pd->dev.kobj, KOBJ_CHANGE);
	typec_unregister_partner(pd->partner);
	pd->partner = NULL;

	if (pd->has_dp)
		pd->has_dp = false;
}

static void handle_hard_reset(struct usbpd *pd)
{
	union power_supply_propval val = {0};

	pd->hard_reset_recvd = false;

	if (pd->requested_current) {
		val.intval = pd->requested_current = 0;
		usbpd_set_psy_iio_property(pd,
				POWER_SUPPLY_PROP_PD_CURRENT_MAX, &val);
	}

	pd->requested_voltage = 5000000;
	val.intval = pd->requested_voltage;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PD_VOLTAGE_MIN, &val);

	pd->in_pr_swap = false;
	val.intval = 0;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PR_SWAP, &val);

	pd->in_explicit_contract = false;
	pd->selected_pdo = pd->requested_pdo = 0;
	pd->rdo = 0;
	rx_msg_cleanup(pd);
	reset_vdm_state(pd);
	kobject_uevent(&pd->dev.kobj, KOBJ_CHANGE);

	if (pd->current_pr == PR_SINK) {
		usbpd_set_state(pd, PE_SNK_TRANSITION_TO_DEFAULT);
	} else {
		s64 delta = ktime_ms_delta(ktime_get(),
				pd->hard_reset_recvd_time);
		pd->current_state = PE_SRC_TRANSITION_TO_DEFAULT;
		if (delta >= PS_HARD_RESET_TIME)
			kick_sm(pd, 0);
		else
			kick_sm(pd, PS_HARD_RESET_TIME - (int)delta);
	}
}

/* Handles current state and determines transitions */
static void usbpd_sm(struct work_struct *w)
{
	struct usbpd *pd = container_of(w, struct usbpd, sm_work);
	int ret;
	struct rx_msg *rx_msg = NULL;
	unsigned long flags;

	usbpd_dbg(&pd->dev, "handle state %s\n",
			usbpd_state_strings[pd->current_state]);

	hrtimer_cancel(&pd->timer);
	pd->sm_queued = false;

	spin_lock_irqsave(&pd->rx_lock, flags);
	if (!list_empty(&pd->rx_q)) {
		rx_msg = list_first_entry(&pd->rx_q, struct rx_msg, entry);
		list_del(&rx_msg->entry);
	}
	spin_unlock_irqrestore(&pd->rx_lock, flags);

	/* Disconnect? */
	if (pd->current_pr == PR_NONE) {
		if (pd->current_state == PE_UNKNOWN &&
				pd->current_dr == DR_NONE)
			goto sm_done;

		handle_disconnect(pd);
		goto sm_done;
	}

	/* Hard reset? */
	if (pd->hard_reset_recvd) {
		handle_hard_reset(pd);
		goto sm_done;
	}

	/* Soft reset? */
	if (IS_CTRL(rx_msg, MSG_SOFT_RESET)) {
		usbpd_dbg(&pd->dev, "Handle soft reset\n");

		if (pd->current_pr == PR_SRC)
			pd->current_state = PE_SRC_SOFT_RESET;
		else if (pd->current_pr == PR_SINK)
			pd->current_state = PE_SNK_SOFT_RESET;
	}

	if (pd->current_state < PE_MAX_STATES &&
			state_handlers[pd->current_state].handle_state)
		state_handlers[pd->current_state].handle_state(pd, rx_msg);
	else
		usbpd_err(&pd->dev, "Unhandled state %s\n",
				usbpd_state_strings[pd->current_state]);
sm_done:
	kfree(rx_msg);

	spin_lock_irqsave(&pd->rx_lock, flags);
	ret = list_empty(&pd->rx_q);
	spin_unlock_irqrestore(&pd->rx_lock, flags);

	/* requeue if there are any new/pending RX messages */
	if (!ret && !pd->sm_queued)
		kick_sm(pd, 0);

	if (!pd->sm_queued)
		pm_relax(&pd->dev);
}

static inline const char *src_current(enum power_supply_typec_mode typec_mode)
{
	switch (typec_mode) {
	case QTI_POWER_SUPPLY_TYPEC_SOURCE_DEFAULT:
		return "default";
	case QTI_POWER_SUPPLY_TYPEC_SOURCE_MEDIUM:
		return "medium - 1.5A";
	case QTI_POWER_SUPPLY_TYPEC_SOURCE_HIGH:
		return "high - 3.0A";
	default:
		return "";
	}
}

static int usbpd_process_typec_mode(struct usbpd *pd,
	enum power_supply_typec_mode typec_mode)
{
	switch (typec_mode) {
	/* Disconnect */
	case QTI_POWER_SUPPLY_TYPEC_NONE:
		if (pd->in_pr_swap) {
			usbpd_dbg(&pd->dev, "Ignoring disconnect due to PR swap\n");
			return 0;
		}

		pd->current_pr = PR_NONE;
		break;

	/* Sink states */
	case QTI_POWER_SUPPLY_TYPEC_SOURCE_DEFAULT:
	case QTI_POWER_SUPPLY_TYPEC_SOURCE_MEDIUM:
	case QTI_POWER_SUPPLY_TYPEC_SOURCE_HIGH:
		usbpd_info(&pd->dev, "Type-C Source (%s) connected\n",
				src_current(typec_mode));

		/* if waiting for SinkTxOk to start an AMS */
		if (pd->spec_rev == USBPD_REV_30 &&
			typec_mode == QTI_POWER_SUPPLY_TYPEC_SOURCE_HIGH &&
			(pd->send_pr_swap || pd->send_dr_swap || pd->vdm_tx))
			break;

		if (pd->current_pr == PR_SINK)
			return 0;

		/*
		 * Unexpected if not in PR swap; need to force disconnect from
		 * source so we can turn off VBUS, Vconn, PD PHY etc.
		 */
		if (pd->current_pr == PR_SRC) {
			usbpd_info(&pd->dev, "Forcing disconnect from source mode\n");
			pd->current_pr = PR_NONE;
			break;
		}

		pd->current_pr = PR_SINK;

		break;

	/* Source states */
	case QTI_POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE:
	case QTI_POWER_SUPPLY_TYPEC_SINK:
		usbpd_info(&pd->dev, "Type-C Sink%s connected\n",
				typec_mode == QTI_POWER_SUPPLY_TYPEC_SINK ?
					"" : " (powered)");

		if (pd->current_pr == PR_SRC)
			return 0;

		pd->current_pr = PR_SRC;
		break;

	case QTI_POWER_SUPPLY_TYPEC_SINK_DEBUG_ACCESSORY:
		usbpd_info(&pd->dev, "Type-C Debug Accessory connected\n");
		break;
	case QTI_POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER:
		usbpd_info(&pd->dev, "Type-C Analog Audio Adapter connected\n");
		break;
	default:
		usbpd_warn(&pd->dev, "Unsupported typec mode:%d\n",
				typec_mode);
		break;
	}

	return 1; /* kick state machine */
}

static void psy_changed_notifier_work(struct work_struct *w)
{
	struct usbpd *pd = container_of(w, struct usbpd, psy_chg_work);
	union power_supply_propval val;
	enum power_supply_typec_mode typec_mode;
	int ret;
	int usb_extcon_state;

	ret = usbpd_get_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_MODE, &val);
	if (ret) {
		usbpd_err(&pd->dev, "Unable to read USB TYPEC_MODE: %d\n", ret);
		return;
	}

	typec_mode = val.intval;

	ret = usbpd_get_psy_iio_property(pd,
			POWER_SUPPLY_PROP_PE_START, &val);
	if (ret) {
		usbpd_err(&pd->dev, "Unable to read USB PROP_PE_START: %d\n",
				ret);
		return;
	}

	/* Don't proceed if PE_START=0; start USB directly if needed */
	if (!val.intval && !pd->pd_connected &&
			typec_mode >= QTI_POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) {
		ret = usbpd_get_psy_iio_property(pd,
				POWER_SUPPLY_PROP_REAL_TYPE, &val);
		if (ret) {
			usbpd_err(&pd->dev, "Unable to read USB TYPE: %d\n",
					ret);
			return;
		}

		if (val.intval == POWER_SUPPLY_TYPE_USB ||
			val.intval == POWER_SUPPLY_TYPE_USB_CDP ||
			val.intval == QTI_POWER_SUPPLY_TYPE_USB_FLOAT) {
			usbpd_dbg(&pd->dev, "typec mode:%d type:%d\n",
				typec_mode, val.intval);
			pd->typec_mode = typec_mode;
			queue_work(pd->wq, &pd->start_periph_work);
		}
		return;
	}

	ret = power_supply_get_property(pd->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &val);
	if (ret) {
		usbpd_err(&pd->dev, "Unable to read USB PRESENT: %d\n", ret);
		return;
	}

	pd->vbus_present = val.intval;

	/*
	 * For sink hard reset, state machine needs to know when VBUS changes
	 *   - when in PE_SNK_TRANSITION_TO_DEFAULT, notify when VBUS falls
	 *   - when in PE_SNK_DISCOVERY, notify when VBUS rises
	 */
	if (typec_mode && ((!pd->vbus_present &&
			pd->current_state == PE_SNK_TRANSITION_TO_DEFAULT) ||
		(pd->vbus_present && pd->current_state == PE_SNK_DISCOVERY))) {
		usbpd_dbg(&pd->dev, "hard reset: typec mode:%d present:%d\n",
			typec_mode, pd->vbus_present);
		pd->typec_mode = typec_mode;

		if (!work_busy(&pd->sm_work))
			kick_sm(pd, 0);
		else
			usbpd_dbg(&pd->dev, "usbpd_sm already running\n");

		return;
	}

	if (pd->typec_mode == typec_mode) {
		if (!((pd->current_dr == DR_NONE) || (pd->current_dr == DR_UFP)))
			return;

		usb_extcon_state = extcon_get_state(pd->extcon, EXTCON_USB);

		if (usb_extcon_state == 0) {
			ret = usbpd_get_psy_iio_property(pd, POWER_SUPPLY_PROP_REAL_TYPE,
								&val);
			if (ret) {
				usbpd_err(&pd->dev, "Unable to read USB PROP_REAL_TYPE: %d\n",
						ret);
				return;
			}

			if (val.intval == POWER_SUPPLY_TYPE_USB ||
					val.intval == POWER_SUPPLY_TYPE_USB_CDP ||
					val.intval == QTI_POWER_SUPPLY_TYPE_USB_FLOAT)
				queue_work(pd->wq, &pd->start_periph_work);
		}
		return;
	}

	pd->typec_mode = typec_mode;

	usbpd_dbg(&pd->dev, "typec mode:%d present:%d orientation:%d\n",
			typec_mode, pd->vbus_present,
			usbpd_get_plug_orientation(pd));

	ret = usbpd_process_typec_mode(pd, typec_mode);

	/* queue state machine due to CC state change */
	if (ret)
		kick_sm(pd, 0);
}

static int psy_changed(struct notifier_block *nb, unsigned long evt, void *ptr)
{
	struct usbpd *pd = container_of(nb, struct usbpd, psy_nb);

	if (ptr != pd->usb_psy || evt != PSY_EVENT_PROP_CHANGED)
		return 0;

	queue_work(pd->wq, &pd->psy_chg_work);
	return 0;
}

static int usbpd_do_swap(struct usbpd *pd, bool dr_swap,
	unsigned int timeout_ms)
{
	if (pd->current_state != PE_SRC_READY &&
			pd->current_state != PE_SNK_READY) {
		usbpd_err(&pd->dev, "%s_role swap not allowed: PD not in Ready state\n",
			dr_swap ? "data" : "power");
		return -EAGAIN;
	}

	mutex_lock(&pd->swap_lock);
	reinit_completion(&pd->is_ready);
	if (dr_swap)
		pd->send_dr_swap = true;
	else
		pd->send_pr_swap = true;

	if (pd->current_state == PE_SRC_READY && !in_src_ams(pd))
		start_src_ams(pd, true);
	else
		kick_sm(pd, 0);

	/* wait for operation to complete */
	if (!wait_for_completion_timeout(&pd->is_ready,
			msecs_to_jiffies(timeout_ms))) {
		usbpd_err(&pd->dev, "%s_role swap timed out\n",
			dr_swap ? "data" : "power");
		mutex_unlock(&pd->swap_lock);
		return -ETIMEDOUT;
	}

	mutex_unlock(&pd->swap_lock);

	return 0;
}

static int usbpd_typec_dr_set(struct typec_port *port,
				enum typec_data_role role)
{
	struct usbpd *pd = (struct usbpd *)typec_get_drvdata(port);
	bool do_swap = false;
	int ret;

	usbpd_dbg(&pd->dev, "Setting data role to %d\n", role);

	if (role == TYPEC_HOST) {
		if (pd->current_dr == DR_UFP)
			do_swap = true;
	} else if (role == TYPEC_DEVICE) {
		if (pd->current_dr == DR_DFP)
			do_swap = true;
	} else {
		usbpd_warn(&pd->dev, "invalid role\n");
		return -EINVAL;
	}

	if (do_swap) {
		ret = usbpd_do_swap(pd, true, 100);
		if (ret)
			return ret;

		if ((role == TYPEC_HOST && pd->current_dr != DR_DFP) ||
			(role == TYPEC_DEVICE && pd->current_dr != DR_UFP)) {
			usbpd_err(&pd->dev, "incorrect state (%s) after data_role swap\n",
					pd->current_dr == DR_DFP ?
					"dfp" : "ufp");
			return -EPROTO;
		}
	}

	return 0;
}

static int usbpd_typec_pr_set(struct typec_port *port,
				enum typec_role role)
{
	struct usbpd *pd = (struct usbpd *)typec_get_drvdata(port);
	bool do_swap = false;
	int ret;

	usbpd_dbg(&pd->dev, "Setting power role to %d\n", role);

	if (role == TYPEC_SOURCE) {
		if (pd->current_pr == PR_SINK)
			do_swap = true;
	} else if (role == TYPEC_SINK) {
		if (pd->current_pr == PR_SRC)
			do_swap = true;
	} else {
		usbpd_warn(&pd->dev, "invalid role\n");
		return -EINVAL;
	}

	if (do_swap) {
		ret = usbpd_do_swap(pd, false, 2000);
		if (ret)
			return ret;

		if ((role == TYPEC_SOURCE && pd->current_pr != PR_SRC) ||
			(role == TYPEC_SINK && pd->current_pr != PR_SINK)) {
			usbpd_err(&pd->dev, "incorrect state (%s) after power_role swap\n",
					pd->current_pr == PR_SRC ?
					"source" : "sink");
			return -EPROTO;
		}
	}

	return 0;
}

static int usbpd_typec_port_type_set(struct typec_port *port,
				enum typec_port_type type)
{
	struct usbpd *pd = (struct usbpd *)typec_get_drvdata(port);
	union power_supply_propval value;
	int wait_count = 5;

	usbpd_dbg(&pd->dev, "Setting mode to %d\n", type);

	if (type == TYPEC_PORT_DRP)
		return 0;

	/*
	 * Forces disconnect on CC and re-establishes connection.
	 * This does not use PD-based PR/DR swap
	 */
	if (type == TYPEC_PORT_SNK)
		pd->forced_pr = QTI_POWER_SUPPLY_TYPEC_PR_SINK;
	else if (type == TYPEC_PORT_SRC)
		pd->forced_pr = QTI_POWER_SUPPLY_TYPEC_PR_SOURCE;

	/* new mode will be applied in disconnect handler */
	set_power_role(pd, PR_NONE);

	/* wait until it takes effect */
	while (pd->forced_pr != QTI_POWER_SUPPLY_TYPEC_PR_NONE && --wait_count)
		msleep(20);

	if (!wait_count)
		goto reset_drp;

	/* if we cannot have a valid connection, fallback to old role */
	wait_count = 5;
	while (pd->current_pr == PR_NONE && --wait_count)
		msleep(300);

	if (!wait_count)
		goto reset_drp;

	return 0;

reset_drp:
	usbpd_err(&pd->dev, "setting mode timed out\n");

	/* Setting it to DRP. HW can figure out new mode */
	value.intval = QTI_POWER_SUPPLY_TYPEC_PR_DUAL;
	usbpd_set_psy_iio_property(pd,
			POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &value);

	return -ETIMEDOUT;
}

static int usbpd_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	int i;

	add_uevent_var(env, "DATA_ROLE=%s", pd->current_dr == DR_DFP ?
			"dfp" : "ufp");

	if (pd->current_pr == PR_SINK) {
		add_uevent_var(env, "POWER_ROLE=sink");
		add_uevent_var(env, "SRC_CAP_ID=%d", pd->src_cap_id);

		for (i = 0; i < ARRAY_SIZE(pd->received_pdos); i++)
			add_uevent_var(env, "PDO%d=%08x", i,
					pd->received_pdos[i]);

		add_uevent_var(env, "REQUESTED_PDO=%d", pd->requested_pdo);
		add_uevent_var(env, "SELECTED_PDO=%d", pd->selected_pdo);
	} else {
		add_uevent_var(env, "POWER_ROLE=source");
		for (i = 0; i < ARRAY_SIZE(default_src_caps); i++)
			add_uevent_var(env, "PDO%d=%08x", i,
					default_src_caps[i]);
	}

	add_uevent_var(env, "RDO=%08x", pd->rdo);
	add_uevent_var(env, "CONTRACT=%s", pd->in_explicit_contract ?
				"explicit" : "implicit");
	add_uevent_var(env, "ALT_MODE=%d", pd->vdm_state == MODE_ENTERED);

	add_uevent_var(env, "SDB=%02x %02x %02x %02x %02x %02x",
			pd->status_db[0], pd->status_db[1], pd->status_db[2],
			pd->status_db[3], pd->status_db[4], pd->status_db[5]);

	return 0;
}

static ssize_t contract_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			pd->in_explicit_contract ?  "explicit" : "implicit");
}
static DEVICE_ATTR_RO(contract);

static ssize_t current_pr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	const char *pr = "none";

	if (pd->current_pr == PR_SINK)
		pr = "sink";
	else if (pd->current_pr == PR_SRC)
		pr = "source";

	return scnprintf(buf, PAGE_SIZE, "%s\n", pr);
}
static DEVICE_ATTR_RO(current_pr);

static ssize_t initial_pr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	const char *pr = "none";

	if (pd->typec_mode >= QTI_POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)
		pr = "sink";
	else if (pd->typec_mode >= QTI_POWER_SUPPLY_TYPEC_SINK)
		pr = "source";

	return scnprintf(buf, PAGE_SIZE, "%s\n", pr);
}
static DEVICE_ATTR_RO(initial_pr);

static ssize_t current_dr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	const char *dr = "none";

	if (pd->current_dr == DR_UFP)
		dr = "ufp";
	else if (pd->current_dr == DR_DFP)
		dr = "dfp";

	return scnprintf(buf, PAGE_SIZE, "%s\n", dr);
}
static DEVICE_ATTR_RO(current_dr);

static ssize_t initial_dr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	const char *dr = "none";

	if (pd->typec_mode >= QTI_POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)
		dr = "ufp";
	else if (pd->typec_mode >= QTI_POWER_SUPPLY_TYPEC_SINK)
		dr = "dfp";

	return scnprintf(buf, PAGE_SIZE, "%s\n", dr);
}
static DEVICE_ATTR_RO(initial_dr);

static ssize_t src_cap_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->src_cap_id);
}
static DEVICE_ATTR_RO(src_cap_id);

/* Dump received source PDOs in human-readable format */
static ssize_t pdo_h_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	int i;
	ssize_t cnt = 0;

	for (i = 0; i < ARRAY_SIZE(pd->received_pdos); i++) {
		u32 pdo = pd->received_pdos[i];

		if (pdo == 0)
			break;

		cnt += scnprintf(&buf[cnt], PAGE_SIZE - cnt, "PDO %d\n", i + 1);

		if (PD_SRC_PDO_TYPE(pdo) == PD_SRC_PDO_TYPE_FIXED) {
			cnt += scnprintf(&buf[cnt], PAGE_SIZE - cnt,
					"\tFixed supply\n"
					"\tDual-Role Power:%d\n"
					"\tUSB Suspend Supported:%d\n"
					"\tExternally Powered:%d\n"
					"\tUSB Communications Capable:%d\n"
					"\tData Role Swap:%d\n"
					"\tPeak Current:%d\n"
					"\tVoltage:%d (mV)\n"
					"\tMax Current:%d (mA)\n",
					PD_SRC_PDO_FIXED_PR_SWAP(pdo),
					PD_SRC_PDO_FIXED_USB_SUSP(pdo),
					PD_SRC_PDO_FIXED_EXT_POWERED(pdo),
					PD_SRC_PDO_FIXED_USB_COMM(pdo),
					PD_SRC_PDO_FIXED_DR_SWAP(pdo),
					PD_SRC_PDO_FIXED_PEAK_CURR(pdo),
					PD_SRC_PDO_FIXED_VOLTAGE(pdo) * 50,
					PD_SRC_PDO_FIXED_MAX_CURR(pdo) * 10);
		} else if (PD_SRC_PDO_TYPE(pdo) == PD_SRC_PDO_TYPE_BATTERY) {
			cnt += scnprintf(&buf[cnt], PAGE_SIZE - cnt,
					"\tBattery supply\n"
					"\tMax Voltage:%d (mV)\n"
					"\tMin Voltage:%d (mV)\n"
					"\tMax Power:%d (mW)\n",
					PD_SRC_PDO_VAR_BATT_MAX_VOLT(pdo) * 50,
					PD_SRC_PDO_VAR_BATT_MIN_VOLT(pdo) * 50,
					PD_SRC_PDO_VAR_BATT_MAX(pdo) * 250);
		} else if (PD_SRC_PDO_TYPE(pdo) == PD_SRC_PDO_TYPE_VARIABLE) {
			cnt += scnprintf(&buf[cnt], PAGE_SIZE - cnt,
					"\tVariable supply\n"
					"\tMax Voltage:%d (mV)\n"
					"\tMin Voltage:%d (mV)\n"
					"\tMax Current:%d (mA)\n",
					PD_SRC_PDO_VAR_BATT_MAX_VOLT(pdo) * 50,
					PD_SRC_PDO_VAR_BATT_MIN_VOLT(pdo) * 50,
					PD_SRC_PDO_VAR_BATT_MAX(pdo) * 10);
		} else if (PD_SRC_PDO_TYPE(pdo) == PD_SRC_PDO_TYPE_AUGMENTED) {
			cnt += scnprintf(&buf[cnt], PAGE_SIZE - cnt,
					"\tProgrammable Power supply\n"
					"\tMax Voltage:%d (mV)\n"
					"\tMin Voltage:%d (mV)\n"
					"\tMax Current:%d (mA)\n",
					PD_APDO_MAX_VOLT(pdo) * 100,
					PD_APDO_MIN_VOLT(pdo) * 100,
					PD_APDO_MAX_CURR(pdo) * 50);
		} else {
			cnt += scnprintf(&buf[cnt], PAGE_SIZE - cnt,
					"Invalid PDO\n");
		}

		cnt += scnprintf(&buf[cnt], PAGE_SIZE - cnt, "\n");
	}

	return cnt;
}
static DEVICE_ATTR_RO(pdo_h);

static ssize_t pdo_n_show(struct device *dev, struct device_attribute *attr,
		char *buf);

#define PDO_ATTR(n) {					\
	.attr	= { .name = __stringify(pdo##n), .mode = 0444 },	\
	.show	= pdo_n_show,				\
}
static struct device_attribute dev_attr_pdos[] = {
	PDO_ATTR(1),
	PDO_ATTR(2),
	PDO_ATTR(3),
	PDO_ATTR(4),
	PDO_ATTR(5),
	PDO_ATTR(6),
	PDO_ATTR(7),
};

static ssize_t pdo_n_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(dev_attr_pdos); i++)
		if (attr == &dev_attr_pdos[i])
			/* dump the PDO as a hex string */
			return scnprintf(buf, PAGE_SIZE, "%08x\n",
					pd->received_pdos[i]);

	usbpd_err(&pd->dev, "Invalid PDO index\n");
	return -EINVAL;
}

static ssize_t select_pdo_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	int src_cap_id;
	int pdo, uv = 0, ua = 0;
	int ret;

	mutex_lock(&pd->swap_lock);

	/* Only allowed if we are already in explicit sink contract */
	if (pd->current_state != PE_SNK_READY) {
		usbpd_err(&pd->dev, "Cannot select new PDO yet\n");
		ret = -EBUSY;
		goto out;
	}

	ret = sscanf(buf, "%d %d %d %d", &src_cap_id, &pdo, &uv, &ua);
	if (ret != 2 && ret != 4) {
		usbpd_err(&pd->dev, "Must specify <src cap id> <PDO> [<uV> <uA>]\n");
		ret = -EINVAL;
		goto out;
	}

	if (src_cap_id != pd->src_cap_id) {
		usbpd_err(&pd->dev, "src_cap_id mismatch.  Requested:%d, current:%d\n",
				src_cap_id, pd->src_cap_id);
		ret = -EINVAL;
		goto out;
	}

	if (pdo < 1 || pdo > 7) {
		usbpd_err(&pd->dev, "invalid PDO:%d\n", pdo);
		ret = -EINVAL;
		goto out;
	}

	ret = pd_select_pdo(pd, pdo, uv, ua);
	if (ret)
		goto out;

	reinit_completion(&pd->is_ready);
	pd->send_request = true;
	kick_sm(pd, 0);

	/* wait for operation to complete */
	if (!wait_for_completion_timeout(&pd->is_ready,
			msecs_to_jiffies(1000))) {
		usbpd_err(&pd->dev, "request timed out\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	/* determine if request was accepted/rejected */
	if (pd->selected_pdo != pd->requested_pdo ||
			pd->current_voltage != pd->requested_voltage) {
		usbpd_err(&pd->dev, "request rejected\n");
		ret = -ECONNREFUSED;
	}

out:
	pd->send_request = false;
	mutex_unlock(&pd->swap_lock);
	return ret ? ret : size;
}

static ssize_t select_pdo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->selected_pdo);
}
static DEVICE_ATTR_RW(select_pdo);

static ssize_t rdo_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);

	/* dump the RDO as a hex string */
	return scnprintf(buf, PAGE_SIZE, "%08x\n", pd->rdo);
}
static DEVICE_ATTR_RO(rdo);

static ssize_t rdo_h_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	int pos = PD_RDO_OBJ_POS(pd->rdo);
	int type = PD_SRC_PDO_TYPE(pd->received_pdos[pos - 1]);
	int len;

	len = scnprintf(buf, PAGE_SIZE, "Request Data Object\n"
			"\tObj Pos:%d\n"
			"\tGiveback:%d\n"
			"\tCapability Mismatch:%d\n"
			"\tUSB Communications Capable:%d\n"
			"\tNo USB Suspend:%d\n",
			PD_RDO_OBJ_POS(pd->rdo),
			PD_RDO_GIVEBACK(pd->rdo),
			PD_RDO_MISMATCH(pd->rdo),
			PD_RDO_USB_COMM(pd->rdo),
			PD_RDO_NO_USB_SUSP(pd->rdo));

	switch (type) {
	case PD_SRC_PDO_TYPE_FIXED:
	case PD_SRC_PDO_TYPE_VARIABLE:
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"(Fixed/Variable)\n"
				"\tOperating Current:%d (mA)\n"
				"\t%s Current:%d (mA)\n",
				PD_RDO_FIXED_CURR(pd->rdo) * 10,
				PD_RDO_GIVEBACK(pd->rdo) ? "Min" : "Max",
				PD_RDO_FIXED_CURR_MINMAX(pd->rdo) * 10);
		break;

	case PD_SRC_PDO_TYPE_BATTERY:
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"(Battery)\n"
				"\tOperating Power:%d (mW)\n"
				"\t%s Power:%d (mW)\n",
				PD_RDO_FIXED_CURR(pd->rdo) * 250,
				PD_RDO_GIVEBACK(pd->rdo) ? "Min" : "Max",
				PD_RDO_FIXED_CURR_MINMAX(pd->rdo) * 250);
		break;

	case PD_SRC_PDO_TYPE_AUGMENTED:
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"(Programmable)\n"
				"\tOutput Voltage:%d (mV)\n"
				"\tOperating Current:%d (mA)\n",
				PD_RDO_PROG_VOLTAGE(pd->rdo) * 20,
				PD_RDO_PROG_CURR(pd->rdo) * 50);
		break;
	}

	return len;
}
static DEVICE_ATTR_RO(rdo_h);

static ssize_t hard_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	u32 val = 0;

	if (kstrtou32(buf, 0, &val) || val != 1)
		return -EINVAL;

	if (val)
		usbpd_set_state(pd, pd->current_pr == PR_SRC ?
				PE_SRC_HARD_RESET : PE_SNK_HARD_RESET);

	return size;
}
static DEVICE_ATTR_WO(hard_reset);

static int trigger_tx_msg(struct usbpd *pd, bool *msg_tx_flag)
{
	int ret = 0;

	/* Only allowed if we are already in explicit sink contract */
	if (pd->current_state != PE_SNK_READY) {
		usbpd_err(&pd->dev, "Cannot send msg\n");
		ret = -EBUSY;
		goto out;
	}

	reinit_completion(&pd->is_ready);
	*msg_tx_flag = true;
	kick_sm(pd, 0);

	/* wait for operation to complete */
	if (!wait_for_completion_timeout(&pd->is_ready,
			msecs_to_jiffies(1000))) {
		usbpd_err(&pd->dev, "request timed out\n");
		ret = -ETIMEDOUT;
	}

out:
	*msg_tx_flag = false;
	return ret;

}

static ssize_t get_src_cap_ext_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i, ret, len = 0;
	struct usbpd *pd = dev_get_drvdata(dev);

	if (pd->spec_rev == USBPD_REV_20)
		return -EINVAL;

	ret = trigger_tx_msg(pd, &pd->send_get_src_cap_ext);
	if (ret)
		return ret;

	for (i = 0; i < PD_SRC_CAP_EXT_DB_LEN; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s0x%02x",
				i ? " " : "", pd->src_cap_ext_db[i]);

	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}
static DEVICE_ATTR_RO(get_src_cap_ext);

static ssize_t get_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i, ret, len = 0;
	struct usbpd *pd = dev_get_drvdata(dev);

	if (pd->spec_rev == USBPD_REV_20)
		return -EINVAL;

	ret = trigger_tx_msg(pd, &pd->send_get_status);
	if (ret)
		return ret;

	for (i = 0; i < PD_STATUS_DB_LEN; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s0x%02x",
				i ? " " : "", pd->status_db[i]);

	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}
static DEVICE_ATTR_RO(get_status);

static ssize_t get_pps_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct usbpd *pd = dev_get_drvdata(dev);

	if (pd->spec_rev == USBPD_REV_20)
		return -EINVAL;

	ret = trigger_tx_msg(pd, &pd->send_get_pps_status);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "0x%08x\n", pd->pps_status_db);
}
static DEVICE_ATTR_RO(get_pps_status);

static ssize_t get_battery_cap_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	u32 val;
	int ret;

	if (pd->spec_rev == USBPD_REV_20 || kstrtou32(buf, 0, &val)
			|| val != 1) {
		pd->get_battery_cap_db = -EINVAL;
		return -EINVAL;
	}

	pd->get_battery_cap_db = val;

	ret = trigger_tx_msg(pd, &pd->send_get_battery_cap);

	return ret ? ret : size;
}

static ssize_t get_battery_cap_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct usbpd *pd = dev_get_drvdata(dev);

	if (pd->get_battery_cap_db == -EINVAL)
		return -EINVAL;

	for (i = 0; i < PD_BATTERY_CAP_DB_LEN; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%s0x%02x",
				i ? " " : "", pd->battery_cap_db[i]);

	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}
static DEVICE_ATTR_RW(get_battery_cap);

static ssize_t get_battery_status_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct usbpd *pd = dev_get_drvdata(dev);
	u32 val;
	int ret;

	if (pd->spec_rev == USBPD_REV_20 || kstrtou32(buf, 0, &val)
			|| val != 1) {
		pd->get_battery_status_db = -EINVAL;
		return -EINVAL;
	}

	pd->get_battery_status_db = val;

	ret = trigger_tx_msg(pd, &pd->send_get_battery_status);

	return ret ? ret : size;
}

static ssize_t get_battery_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usbpd *pd = dev_get_drvdata(dev);

	if (pd->get_battery_status_db == -EINVAL)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "0x%08x\n", pd->battery_sts_dobj);
}
static DEVICE_ATTR_RW(get_battery_status);

static struct attribute *usbpd_attrs[] = {
	&dev_attr_contract.attr,
	&dev_attr_initial_pr.attr,
	&dev_attr_current_pr.attr,
	&dev_attr_initial_dr.attr,
	&dev_attr_current_dr.attr,
	&dev_attr_src_cap_id.attr,
	&dev_attr_pdo_h.attr,
	&dev_attr_pdos[0].attr,
	&dev_attr_pdos[1].attr,
	&dev_attr_pdos[2].attr,
	&dev_attr_pdos[3].attr,
	&dev_attr_pdos[4].attr,
	&dev_attr_pdos[5].attr,
	&dev_attr_pdos[6].attr,
	&dev_attr_select_pdo.attr,
	&dev_attr_rdo.attr,
	&dev_attr_rdo_h.attr,
	&dev_attr_hard_reset.attr,
	&dev_attr_get_src_cap_ext.attr,
	&dev_attr_get_status.attr,
	&dev_attr_get_pps_status.attr,
	&dev_attr_get_battery_cap.attr,
	&dev_attr_get_battery_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(usbpd);

static struct class usbpd_class = {
	.name = "usbpd",
	.owner = THIS_MODULE,
	.dev_uevent = usbpd_uevent,
	.dev_groups = usbpd_groups,
};

static int match_usbpd_device(struct device *dev, const void *data)
{
	return dev->parent == data;
}

static void devm_usbpd_put(struct device *dev, void *res)
{
	struct usbpd **ppd = res;

	put_device(&(*ppd)->dev);
}

struct usbpd *devm_usbpd_get_by_phandle(struct device *dev, const char *phandle)
{
	struct usbpd **ptr, *pd = NULL;
	struct device_node *pd_np;
	struct platform_device *pdev;
	struct device *pd_dev;

	if (!usbpd_class.p) /* usbpd_init() not yet called */
		return ERR_PTR(-EAGAIN);

	if (!dev->of_node)
		return ERR_PTR(-EINVAL);

	pd_np = of_parse_phandle(dev->of_node, phandle, 0);
	if (!pd_np)
		return ERR_PTR(-ENXIO);

	pdev = of_find_device_by_node(pd_np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	pd_dev = class_find_device(&usbpd_class, NULL, &pdev->dev,
			match_usbpd_device);
	if (!pd_dev) {
		platform_device_put(pdev);
		/* device was found but maybe hadn't probed yet, so defer */
		return ERR_PTR(-EPROBE_DEFER);
	}

	ptr = devres_alloc(devm_usbpd_put, sizeof(*ptr), GFP_KERNEL);
	if (!ptr) {
		put_device(pd_dev);
		platform_device_put(pdev);
		return ERR_PTR(-ENOMEM);
	}

	pd = dev_get_drvdata(pd_dev);
	if (!pd)
		return ERR_PTR(-EPROBE_DEFER);

	*ptr = pd;
	devres_add(dev, ptr);

	return pd;
}
EXPORT_SYMBOL(devm_usbpd_get_by_phandle);

static void usbpd_release(struct device *dev)
{
	struct usbpd *pd = container_of(dev, struct usbpd, dev);

	kfree(pd);
}

static int num_pd_instances;

const struct typec_operations typec_ops = {
	.dr_set = usbpd_typec_dr_set,
	.pr_set = usbpd_typec_pr_set,
	.port_type_set = usbpd_typec_port_type_set,
};

/**
 * usbpd_create - Create a new instance of USB PD protocol/policy engine
 * @parent - parent device to associate with
 *
 * This creates a new usbpd class device which manages the state of a
 * USB PD-capable port. The parent device that is passed in should be
 * associated with the physical device port, e.g. a PD PHY.
 *
 * Return: struct usbpd pointer, or an ERR_PTR value
 */
struct usbpd *usbpd_create(struct device *parent,
					struct pd_phy_ops *pdphy_ops)
{
	int ret;
	struct usbpd *pd;
	union power_supply_propval val = {0};
	int i;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	device_initialize(&pd->dev);
	pd->dev.class = &usbpd_class;
	pd->dev.parent = parent;
	pd->dev.release = usbpd_release;
	dev_set_drvdata(&pd->dev, pd);

	ret = dev_set_name(&pd->dev, "usbpd%d", num_pd_instances++);
	if (ret)
		goto free_pd;

	ret = device_init_wakeup(&pd->dev, true);
	if (ret)
		goto free_pd;

	ret = device_add(&pd->dev);
	if (ret)
		goto free_pd;

	pd->wq = alloc_ordered_workqueue("usbpd_wq", WQ_FREEZABLE | WQ_HIGHPRI);
	if (!pd->wq) {
		ret = -ENOMEM;
		goto del_pd;
	}
	INIT_WORK(&pd->sm_work, usbpd_sm);
	INIT_WORK(&pd->psy_chg_work, psy_changed_notifier_work);
	INIT_WORK(&pd->start_periph_work, start_usb_peripheral_work);
	hrtimer_init(&pd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pd->timer.function = pd_timeout;
	mutex_init(&pd->swap_lock);
	mutex_init(&pd->svid_handler_lock);

	pd->usb_psy = power_supply_get_by_name("usb");
	if (!pd->usb_psy) {
		usbpd_dbg(&pd->dev, "Could not get USB power_supply, deferring probe\n");
		ret = -EPROBE_DEFER;
		goto destroy_wq;
	}

	if (!pd->bat_psy)
		pd->bat_psy = power_supply_get_by_name("battery");

	if (pd->bat_psy) {
		if (!power_supply_get_property(pd->bat_psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &val))
			pd->bat_voltage_max = val.intval;

		if (!power_supply_get_property(pd->bat_psy,
			POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &val))
			pd->bat_charge_full = val.intval;
	}

	for (i = 0; i < POWER_SUPPLY_IIO_PROP_MAX; i++) {
		pd->iio_channels[i] = devm_iio_channel_get(parent,
						usbpd_iio_channel_map[i]);
		if (IS_ERR(pd->iio_channels[i])) {
			usbpd_err(&pd->dev, "failed to get %s iio channel\n",
						usbpd_iio_channel_map[i]);
			ret = PTR_ERR(pd->iio_channels[i]);
			goto put_psy;
		}
	}

	/*
	 * associate extcon with the parent dev as it could have a DT
	 * node which will be useful for extcon_get_edev_by_phandle()
	 */
	pd->extcon = devm_extcon_dev_allocate(parent, usbpd_extcon_cable);
	if (IS_ERR(pd->extcon)) {
		usbpd_err(&pd->dev, "failed to allocate extcon device\n");
		ret = PTR_ERR(pd->extcon);
		goto put_psy;
	}

	ret = devm_extcon_dev_register(parent, pd->extcon);
	if (ret) {
		usbpd_err(&pd->dev, "failed to register extcon device\n");
		goto put_psy;
	}

	/* Support reporting polarity and speed via properties */
	extcon_set_property_capability(pd->extcon, EXTCON_USB,
			EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(pd->extcon, EXTCON_USB,
			EXTCON_PROP_USB_SS);
	extcon_set_property_capability(pd->extcon, EXTCON_USB_HOST,
			EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(pd->extcon, EXTCON_USB_HOST,
			EXTCON_PROP_USB_SS);

	pd->num_sink_caps = device_property_read_u32_array(parent,
			"qcom,default-sink-caps", NULL, 0);
	if (pd->num_sink_caps > 0) {
		int i;
		u32 sink_caps[14];

		if (pd->num_sink_caps % 2 || pd->num_sink_caps > 14) {
			ret = -EINVAL;
			usbpd_err(&pd->dev, "default-sink-caps must be specified as voltage/current, max 7 pairs\n");
			goto put_psy;
		}

		ret = device_property_read_u32_array(parent,
				"qcom,default-sink-caps", sink_caps,
				pd->num_sink_caps);
		if (ret) {
			usbpd_err(&pd->dev, "Error reading default-sink-caps\n");
			goto put_psy;
		}

		pd->num_sink_caps /= 2;

		for (i = 0; i < pd->num_sink_caps; i++) {
			int v = sink_caps[i * 2] / 50;
			int c = sink_caps[i * 2 + 1] / 10;

			pd->sink_caps[i] =
				PD_SNK_PDO_FIXED(0, 0, 0, 0, 0, v, c);
		}

		/* First PDO includes additional capabilities */
		pd->sink_caps[0] |= PD_SNK_PDO_FIXED(1, 0, 0, 1, 1, 0, 0);
	} else {
		memcpy(pd->sink_caps, default_snk_caps,
				sizeof(default_snk_caps));
		pd->num_sink_caps = ARRAY_SIZE(default_snk_caps);
	}

	/*
	 * Register a Type-C class instance (/sys/class/typec/portX).
	 * Note this is different than the /sys/class/usbpd/ created above.
	 */
	pd->typec_caps.type = TYPEC_PORT_DRP;
	pd->typec_caps.data = TYPEC_PORT_DRD;
	pd->typec_caps.revision = 0x0130;
	pd->typec_caps.pd_revision = 0x0300;
	pd->typec_caps.ops = &typec_ops;
	pd->typec_caps.driver_data = pd;
	pd->partner_desc.identity = &pd->partner_identity;

	ret = get_connector_type(pd);
	if (ret < 0)
		goto put_psy;

	/* For non-TypeC connector, it will be handled elsewhere */
	if (ret != QTI_POWER_SUPPLY_CONNECTOR_MICRO_USB) {
		pd->typec_port = typec_register_port(parent, &pd->typec_caps);
		if (IS_ERR(pd->typec_port)) {
			usbpd_err(&pd->dev, "could not register typec port\n");
			goto put_psy;
		}
	}

	pd->pdphy_ops = pdphy_ops;
	pd->current_pr = PR_NONE;
	pd->current_dr = DR_NONE;
	list_add_tail(&pd->instance, &_usbpd);

	spin_lock_init(&pd->rx_lock);
	INIT_LIST_HEAD(&pd->rx_q);
	INIT_LIST_HEAD(&pd->svid_handlers);
	init_completion(&pd->is_ready);
	init_completion(&pd->tx_chunk_request);

	pd->psy_nb.notifier_call = psy_changed;
	ret = power_supply_reg_notifier(&pd->psy_nb);
	if (ret)
		goto del_inst;

	/* force read initial power_supply values */
	psy_changed(&pd->psy_nb, PSY_EVENT_PROP_CHANGED, pd->usb_psy);

	return pd;

del_inst:
	list_del(&pd->instance);
put_psy:
	power_supply_put(pd->usb_psy);
destroy_wq:
	destroy_workqueue(pd->wq);
del_pd:
	device_del(&pd->dev);
free_pd:
	num_pd_instances--;
	put_device(&pd->dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(usbpd_create);

/**
 * usbpd_destroy - Removes and frees a usbpd instance
 * @pd: the instance to destroy
 */
void usbpd_destroy(struct usbpd *pd)
{
	if (!pd)
		return;

	list_del(&pd->instance);
	power_supply_unreg_notifier(&pd->psy_nb);
	power_supply_put(pd->usb_psy);
	if (pd->bat_psy)
		power_supply_put(pd->bat_psy);
	destroy_workqueue(pd->wq);
	device_unregister(&pd->dev);
}
EXPORT_SYMBOL(usbpd_destroy);

static int __init usbpd_init(void)
{
	usbpd_ipc_log = ipc_log_context_create(NUM_LOG_PAGES, "usb_pd", 0);
	return class_register(&usbpd_class);
}
module_init(usbpd_init);

static void __exit usbpd_exit(void)
{
	class_unregister(&usbpd_class);
}
module_exit(usbpd_exit);

MODULE_DESCRIPTION("USB Power Delivery Policy Engine");
MODULE_LICENSE("GPL");
