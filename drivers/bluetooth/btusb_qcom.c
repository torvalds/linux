// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Bluetooth USB transport-specific support
 *
 * Abbreviations:
 *   BTC   - BT controller
 *   PERI  - Peripheral subsystem of a multi-subsys BTC
 *   TME-L - Trust Management Engine Lite subsystem of a multi-subsys BTC
 *   DFU   - Device Firmware Update
 *   EDL   - Embedded Downloader
 *   TLV   - Type-Length-Value, a firmware file format
 *   VSC   - Vendor-Specific Command
 *   VSE   - Vendor-Specific Event
 *   CCE   - Command Complete Event
 *   CSE   - Command Status Event
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/args.h>
#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/unaligned.h>
#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/coredump.h>
#include <net/bluetooth/hci_core.h>

#include "btusb_qcom.h"

/*
 * ============================================================================
 * Supported BTCs and their configuration
 * ============================================================================
 */

/*
 * enum qbtc_category - driver-perspective BTC category
 * @QBTC_CAT_LEGACY: legacy
 * @QBTC_CAT_UNIFIED: unified HCI, VSC gets CCE as response
 * @QBTC_CAT_MSUBSYS: multi-subsys, always has PERI
 * @QBTC_CAT_MAX: number of categories
 */
enum qbtc_category {
	QBTC_CAT_LEGACY,
	QBTC_CAT_UNIFIED,
	QBTC_CAT_MSUBSYS,
	QBTC_CAT_MAX,
};

static const char *qbtc_category_name(int category)
{
	const char *category_name = "unknown";

	switch (category) {
	case QBTC_CAT_LEGACY:
		category_name = "legacy";
		break;
	case QBTC_CAT_UNIFIED:
		category_name = "unified";
		break;
	case QBTC_CAT_MSUBSYS:
		category_name = "multi-subsys";
		break;
	default:
		break;
	}

	return category_name;
}

/* flags for BTC info and default configuration */

/* BTC has PERI / TME-L subsystem */
#define QBT_FLAG_HAS_PERI		BIT(0)
#define QBT_FLAG_HAS_TMEL		BIT(1)
/* BTC supports AOSP / MSFT vendor extension */
#define QBT_FLAG_AOSP_EXT		BIT(4)
#define QBT_FLAG_MSFT_EXT		BIT(5)
/* BTC supports software reset */
#define QBT_FLAG_SW_RESET		BIT(6)
/* BTC supports memdump */
#define QBT_FLAG_MEMDUMP		BIT(7)
/* take foundry as a factor to select NVM */
#define QBT_FLAG_FOUNDRY_NVM		BIT(8)
/* select NVM based on board ID to download */
#define QBT_FLAG_BID_NVM		BIT(9)
/* fall back to the default NVM if no board-ID-specific NVM exists */
#define QBT_FLAG_NVM_FALLBACK		BIT(10)
/* reset PERI HCI by QHCI instead of QDFU */
#define QBT_FLAG_RESET_PERI_HCI		BIT(11)
/* trigger memdump on command timeout */
#define QBT_FLAG_CMD_TIMEOUT_MEMDUMP	BIT(12)
/* reserve bits [31:24] for board-level flags */
#define QBT_FLAG_BTC_CFG_MASK		GENMASK(23, 0)

/* rare board ID to custom firmware directroy map */
struct qbtc_bid_fwdir {
	u16 board_id;
	const char *fw_dir;
};

/*
 * struct qbtc_info - BTC information and default configuration
 * @category: driver-perspective BTC category (legacy/unified/multi-subsys)
 * @flags: QBT_FLAG_* — BTC attributes and default configuration
 * @fw_dir: firmware folder, "qca" if NULL
 * @custom_fw_table: {}-terminated array or NULL
 */
struct qbtc_info {
	enum qbtc_category category;
	unsigned long flags;
	const char *fw_dir;
	const struct qbtc_bid_fwdir *custom_fw_table;
};

/*
 * struct qbtc_id - BTC ID entry in qbtc_id_table[] below
 * @rom_version: ID to match against rom_version read from BTC
 * @name: human-readable BTC name
 * @btc_info: BTC information and default configuration, != NULL
 */
struct qbtc_id {
	u32		rom_version;
	const char	*name;
	const struct qbtc_info *btc_info;
};

/* multi-subsys BTC here */
#define QBTC_INFO_FLAGS_MSUBSYS	(QBT_FLAG_HAS_PERI | QBT_FLAG_AOSP_EXT | \
				 QBT_FLAG_MSFT_EXT | QBT_FLAG_SW_RESET | \
				 QBT_FLAG_MEMDUMP | QBT_FLAG_BID_NVM | \
				 QBT_FLAG_NVM_FALLBACK | QBT_FLAG_CMD_TIMEOUT_MEMDUMP)

static const struct qbtc_info qbtc_msubsys_qcc2072 = {
	.category = QBTC_CAT_MSUBSYS,
	.flags    = QBTC_INFO_FLAGS_MSUBSYS,
	.fw_dir   = "qca/QCC2072",
};

static const struct qbtc_id qbtc_id_table[] = {
	{ 0x00220100, "QCC2072 1.x", &qbtc_msubsys_qcc2072 },
	{ }
};

/*
 * ============================================================================
 * Qualcomm DFU for rampatch/NVM download
 * ============================================================================
 */

/* QDFU USB vendor command codes (bRequest) */
#define QDFU_BT_CMD_VSC_REQ_DOWNLOAD_LOCAL	0x01
#define QDFU_BT_CMD_CHECK_TARGET_STATE	0x05
#define QDFU_BT_CMD_GET_TARGET_VERSION	0x09
#define QDFU_BT_CMD_VSC_REQ_DOWNLOAD_REMOTE	0x10
#define QDFU_BT_CMD_RESET_PERI_HCI			0x11
#define QDFU_BT_CMD_ACTIVATE_REMOTE_BTSS	0x12
#define QDFU_BT_CMD_BT_ENABLE_RESET		0x13

/* QDFU target status bits (u8 bitmask from QDFU_BT_CMD_CHECK_TARGET_STATE) */
#define QDFU_BT_STATE_LOADING_LOCAL		BIT(3)
#define QDFU_BT_STATE_PATCHED_REMOTE	BIT(4)
#define QDFU_BT_STATE_NVMED_REMOTE		BIT(5)
#define QDFU_BT_STATE_NVMED_LOCAL		BIT(6)
#define QDFU_BT_STATE_PATCHED_LOCAL		BIT(7)

/* QDFU_BT_CMD_GET_TARGET_VERSION response */
struct qdfu_bt_version {
	__le32	rom_version;
	__le32	patch_version;
	__le32	soc_ver;
	__be16	board_id;
	__le16	flag;
	u8	reserved[4];
} __packed;

struct qdfu_bt_id {
	u32 rom_version;
	u32 patch_version;
	u32 soc_id;
	u16 board_id;
};

static inline int qdfu_recv_vendor_req(struct hci_dev *hdev, u8 request,
				       u16 value, void *buf, u16 size)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);

	return usb_control_msg_recv(xport_data->udev, 0, request,
				    USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
				    value, 0, buf, size,
				    USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
}

static inline int qdfu_send_vendor_req(struct hci_dev *hdev, u8 request,
				       u16 value, const void *buf, u16 size)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);

	return usb_control_msg_send(xport_data->udev, 0, request,
				    USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
				    value, 0, buf, size,
				    USB_CTRL_SET_TIMEOUT, GFP_KERNEL);
}

static int qdfu_vsc_req_download(struct hci_dev *hdev, u8 request,
				 const void *buf, u16 size)
{
	int ret;

	if (request != QDFU_BT_CMD_VSC_REQ_DOWNLOAD_LOCAL &&
	    request != QDFU_BT_CMD_VSC_REQ_DOWNLOAD_REMOTE) {
		bt_dev_err(hdev,
			   "QDFU download invalid request code 0x%02x", request);
		return -EINVAL;
	}

	ret = qdfu_send_vendor_req(hdev, request, 0, buf, size);
	if (ret)
		bt_dev_err(hdev,
			   "QDFU download request 0x%02x failed: %pe",
			   request, ERR_PTR(ret));

	return ret;
}

static int qdfu_get_target_state(struct hci_dev *hdev, u8 *state_ptr)
{
	u8 state;
	int ret;

	ret = qdfu_recv_vendor_req(hdev, QDFU_BT_CMD_CHECK_TARGET_STATE, 0,
				   &state, sizeof(state));
	if (!ret)
		*state_ptr = state;
	else
		bt_dev_err(hdev,
			   "QDFU get target state failed: %pe",
			   ERR_PTR(ret));

	return ret;
}

static int qdfu_get_target_version(struct hci_dev *hdev, struct qdfu_bt_id *id_info)
{
	struct qdfu_bt_version dfu_ver;
	u16 board_id = 0;
	int ret;

	ret = qdfu_recv_vendor_req(hdev, QDFU_BT_CMD_GET_TARGET_VERSION, 0,
				   &dfu_ver, sizeof(dfu_ver));
	if (ret) {
		bt_dev_err(hdev,
			   "QDFU get target version failed: %pe",
			   ERR_PTR(ret));
		return ret;
	}

	id_info->rom_version = le32_to_cpu(dfu_ver.rom_version);
	id_info->patch_version = le32_to_cpu(dfu_ver.patch_version);
	id_info->soc_id = le32_to_cpu(dfu_ver.soc_ver);

	if ((le16_to_cpu(dfu_ver.flag) >> 8) == 0x80)
		board_id = be16_to_cpu(dfu_ver.board_id);

	/* Take 0xffff as invalid board ID */
	if (board_id == 0xffff)
		board_id = 0;

	id_info->board_id = board_id;

	return 0;
}

static int qdfu_activate_remote_btss(struct hci_dev *hdev, bool on,
				     unsigned int wait_us)
{
	u8 status = 0;
	int ret;

	ret = qdfu_recv_vendor_req(hdev, QDFU_BT_CMD_ACTIVATE_REMOTE_BTSS, on,
				   &status, sizeof(status));
	if (ret) {
		bt_dev_err(hdev, "QDFU turn %s remote BTSS failed: %pe",
			   str_on_off(on), ERR_PTR(ret));
		return ret;
	}

	switch (status) {
	case 0:
		bt_dev_dbg(hdev, "QDFU Remote BTSS turned %s", str_on_off(on));
		fsleep(wait_us);
		break;
	case 0x17:
		bt_dev_dbg(hdev, "QDFU Remote BTSS already %s", str_on_off(on));
		break;
	default:
		bt_dev_err(hdev,
			   "QDFU turn remote BTSS %s failed, unexpected status 0x%02x",
			   str_on_off(on), status);
		ret = -ENODEV;
	}

	return ret;
}

static int qdfu_reset_peri_hci(struct hci_dev *hdev)
{
	int ret;

	ret = qdfu_send_vendor_req(hdev, QDFU_BT_CMD_RESET_PERI_HCI, 0, NULL, 0);
	if (ret) {
		bt_dev_err(hdev, "PERI HCI reset via QDFU failed: %pe",
			   ERR_PTR(ret));
		return ret;
	}
	bt_dev_info(hdev, "PERI HCI reset via QDFU succeeded");
	fsleep(20 * 1000);

	return 0;
}

static int qdfu_sw_reset(struct hci_dev *hdev)
{
	int ret;

	ret = qdfu_send_vendor_req(hdev, QDFU_BT_CMD_BT_ENABLE_RESET, 0, NULL, 0);
	/* SW reset succeeds even if the request returns an error code */
	if (ret)
		bt_dev_dbg(hdev, "QDFU SW reset failed: %pe", ERR_PTR(ret));

	bt_dev_info(hdev, "QDFU SW reset succeeded");

	return 0;
}

/*
 * qdfu_poll_state - wait for DFU target status flags to reach a wanted state
 * @hdev: the HCI device to poll
 * @state_ptr: optional storage for the last status read; may be NULL
 * @set: true to wait until @flags are all set, false until all cleared
 * @flags: the status flag bits to wait on
 *
 * Repeatedly reads the DFU target status until @flags reach the requested
 * state, a status read fails, or the overall timeout expires.
 *
 * Return: 0 on success, -ETIMEDOUT on timeout, or a negative errno on read
 *	failure.
 */
static int qdfu_poll_state(struct hci_dev *hdev, u8 *state_ptr, bool set, u8 flags)
{
	int err, ret;
	u8 dfu_state;

	if (!state_ptr)
		state_ptr = &dfu_state;

	ret = read_poll_timeout(qdfu_get_target_state, err,
				err || (set ? (*state_ptr & flags) == flags
					    : !(*state_ptr & flags)),
				5 * 1000,
				3000 * 1000, true,
				hdev, state_ptr);
	if (ret) {
		bt_dev_err(hdev,
			   "Timed out waiting for QDFU state flags 0x%02x to be %s: %pe",
			   flags, set ? "set" : "cleared", ERR_PTR(ret));
		return ret;
	}

	return err;
}

static inline int qdfu_reset_msubsys_bt(struct hci_dev *hdev)
{
	int ret;

	ret = qdfu_activate_remote_btss(hdev, false, 100 * 1000);
	if (!ret)
		ret = qdfu_activate_remote_btss(hdev, true, 100 * 1000);
	if (!ret)
		bt_dev_info(hdev, "QDFU reset msubsys BT succeeded");

	return ret;
}

/*
 * ============================================================================
 * Common definitions, per-device struct btqcom_data, and PERI frame format
 * ============================================================================
 */

/* BTC version, read out via an HCI command */
struct qhci_btc_ver {
	u32 product_id;
	u32 soc_ver;
	u16 rom_ver;
	u16 patch_ver;
	u8  sec_ver;
};

/* Subsystem field appears in a PERI command/response/event */
enum qhci_subsys {
	QHCI_SUBSYS_PERI,
	QHCI_SUBSYS_BT,
	QHCI_SUBSYS_UWB,
	QHCI_SUBSYS_TMEL,
	QHCI_SUBSYS_MAX,
};

/*
 * The pseudo QHCI subsys: for BT-only BTC, or when the memdump comes
 * from the BT channel (HCI_EVENT_PKT or HCI_ACLDATA_PKT) on a msubsys
 * BTC, there is no real subsys — it stands in for these cases.
 */
#define QHCI_SUBSYS_INVALID	QHCI_SUBSYS_MAX

static const char *qhci_subsys_name(int subsys)
{
	const char *subsys_name = "unknown";

	switch (subsys) {
	case QHCI_SUBSYS_PERI:
		subsys_name = "PERI";
		break;
	case QHCI_SUBSYS_BT:
		subsys_name = "PERI_BT";
		break;
	case QHCI_SUBSYS_UWB:
		subsys_name = "UWB";
		break;
	case QHCI_SUBSYS_TMEL:
		subsys_name = "TME-L";
		break;
	case QHCI_SUBSYS_INVALID:
		subsys_name = "BT";
		break;
	default:
		break;
	}

	return subsys_name;
}

/* BTC subsystems we care about that support the BT function unit */
enum qbtc_subsys {
	QBTC_SUBSYS_PERI,
	QBTC_SUBSYS_TMEL,
	QBTC_SUBSYS_MAX,
};

/*
 * The pseudo BTC subsys stands in for the BT function unit, not a
 * real subsys, when building a firmware file path.
 */
#define QBTC_SUBSYS_INVALID QBTC_SUBSYS_MAX

static const char *qbtc_subsys_name(int subsys)
{
	const char *subsys_name = "unknown";

	switch (subsys) {
	case QBTC_SUBSYS_PERI:
		subsys_name = "PERI";
		break;
	case QBTC_SUBSYS_TMEL:
		subsys_name = "TME-L";
		break;
	case QBTC_SUBSYS_INVALID:
		subsys_name = "BT";
		break;
	default:
		break;
	}

	return subsys_name;
}

/*
 * struct qbtc_subsys_data - per-subsys data
 * @ver: subsys version
 * @board_id: subsys board ID
 * @build_info: the subsys firmware's build info string
 */
struct qbtc_subsys_data {
	struct qhci_btc_ver ver;
	u16	board_id;
	char	build_info[160];
};

/* simple command payload */
struct qhci_cp_simple {
	u8	sub_opcode;
} __packed;

/* simple CCE(response) payload */
struct qhci_rp_simple {
	u8	status;
} __packed;

/* generic CCE(response) payload */
struct qhci_rp_generic {
	u8		status;
	u8		sub_opcode;
} __packed;

/* common VSE payload */
struct qhci_vse_comm {
	u8   ev_class;
	u8   ev_type;
} __packed;
#define QHCI_VSE_COMM_SIZE	(sizeof(struct qhci_vse_comm))

#define QHCI_MEMDUMP_SEQ_LAST		0xFFFF
#define QBT_EV_CLASS_DATALOG 0x01
#define QBT_EV_TYPE_MEMDUMP  0x08

/*
 * struct qhci_vse_memdump - one memdump segment
 * @seqno: segment sequence number; 0 = first, QHCI_MEMDUMP_SEQ_LAST = last
 * @subsys: subsystem this segment belongs to (see enum qhci_subsys)
 * @segdata: payload of a middle or last segment
 * @dump_size: total dump size, valid only in the first segment (@seqno == 0)
 * @first_segdata: payload of the first segment, following @dump_size
 */
struct qhci_vse_memdump {
	__le16 seqno;
	u8     subsys;
	union {
		u8 segdata[0];
		struct {
			__le32 dump_size;
			u8 first_segdata[];
		} __packed;
	};
} __packed;

#define QHCI_MEMDUMP_SEGDATA_GAP \
	(offsetof(struct qhci_vse_memdump, first_segdata) - \
	 offsetof(struct qhci_vse_memdump, segdata))

enum qhci_req_state {
	QHCI_REQ_DONE,
	QHCI_REQ_PEND,
	QHCI_REQ_CANCELED,
	/* unused for now */
	QHCI_REQ_STOP,
};

#define QHCI_OPCODE_INVALID		HCI_OP_NOP
#define QHCI_SUB_OPCODE_INVALID		0xff
#define QHCI_REQ_EVENT_MAX		2

/*
 * struct qhci_req_spec - request/response specification
 * @opcode: command opcode, ignored if invalid
 * @sub_opcode: command sub-opcode, ignored if invalid
 * @event: a sequence of events, ending with an invalid marker
 */
struct qhci_req_spec {
	u16	opcode;
	u8	sub_opcode;
	u8	event[QHCI_REQ_EVENT_MAX + 1];
};

/*
 * struct qhci_req - per-request data, often stack allocated by the requester
 * @pkt_type: the packet type to sync
 * @spec: the request spec to sync
 * @req_rsp: response skb on success, NULL on no data, or ERR_PTR() on failure
 * @req_result: >= 0 an HCI status code, or a negative errno on failure
 * @event_idx: cursor into @spec->event
 */
struct qhci_req {
	u8				pkt_type;
	const struct qhci_req_spec	*spec;
	struct sk_buff			*req_rsp;
	int				req_result;
	u8				event_idx;
};

/* board has a BT_EN pin to reset BTC */
#define QBT_FLAG_HW_RESET		BIT(24)

/*
 * enum qbt_work_bit - bits in btqcom_data.work_flags
 * @QBT_WORK_RESET_HDEV: request to reset hdev
 */
enum qbt_work_bit {
	QBT_WORK_RESET_HDEV,
};

/*
 * enum qbt_misc_bit - bits in btqcom_data.misc_flags
 * @QBT_MISC_MEMDUMP_PERI: PERI memdump
 * @QBT_MISC_MEMDUMP_BT: BT memdump
 * @QBT_MISC_MEMDUMP_UWB: UWB memdump, never happens here
 * @QBT_MISC_MEMDUMP_TMEL: TME-L memdump
 * @QBT_MISC_MEMDUMP_INCOMING: memdump is arriving from the BTC
 * @QBT_MISC_RESET_ACTIVE: a reset is in progress
 * @QBT_MISC_HWERR_PERI: PERI hardware error event happens
 * @QBT_MISC_HWERR_BT: BT hardware error event happens
 * @QBT_MISC_CMD_TIMEOUT: command timeout happens
 *
 * The memdump bits are indexed by enum qhci_subsys, so a subsystem's bit can
 * be derived from its QHCI subsys value.
 */
enum qbt_misc_bit {
	QBT_MISC_MEMDUMP_PERI = QHCI_SUBSYS_PERI,
	QBT_MISC_MEMDUMP_BT = QHCI_SUBSYS_BT,
	QBT_MISC_MEMDUMP_UWB = QHCI_SUBSYS_UWB,
	QBT_MISC_MEMDUMP_TMEL = QHCI_SUBSYS_TMEL,
	QBT_MISC_MEMDUMP_INCOMING,
	QBT_MISC_RESET_ACTIVE,
	QBT_MISC_HWERR_PERI,
	QBT_MISC_HWERR_BT,
	QBT_MISC_CMD_TIMEOUT,
};

/* each subsys's memdump pending flag in btqcom_data.md_pending_flags */
#define QMD_FLAG_PENDING_PERI	BIT(QBT_MISC_MEMDUMP_PERI)
#define QMD_FLAG_PENDING_BT	BIT(QBT_MISC_MEMDUMP_BT)
#define QMD_FLAG_PENDING_TMEL	BIT(QBT_MISC_MEMDUMP_TMEL)
#define QMD_FLAG_PENDING_MASK	(QMD_FLAG_PENDING_PERI | \
				 QMD_FLAG_PENDING_BT | \
				 QMD_FLAG_PENDING_TMEL)

static inline int qmd_subsys_to_bit(int qhci_subsys)
{
	return qhci_subsys == QHCI_SUBSYS_INVALID ?
	       QBT_MISC_MEMDUMP_BT : qhci_subsys;
}

static inline unsigned long qmd_subsys_to_flag(int qhci_subsys)
{
	return BIT(qmd_subsys_to_bit(qhci_subsys));
}

/*
 * struct btqcom_memdump - collect memdump from a QHCI subsys
 * @size: total memdump size to collect
 * @subsys: QHCI subsys this memdump belongs to
 * @seqno: next expected sequence number
 * @from: which channel this memdump comes from, marked by hci_skb_pkt_type()
 * @submit_size: bytes submitted to HCI devcoredump for the subsys
 */
struct btqcom_memdump {
	u32 size;
	int subsys;
	u16 seqno;
	u8 from;

	u32 submit_size;
};

/*
 * struct btqcom_data - transport-independent per-device common data, allocated as hci priv
 * @drv_name: driver name
 * @btc_name: human-readable BTC name
 * @category: driver-perspective BTC category (legacy/unified/multi-subsys)
 * @flags: QBT_FLAG_* flags for BTC/board and default config
 * @hdev: the owning HCI device
 * @xport_data: opaque transport-specific data (e.g. struct btusb_qcom for USB)
 * @inited: whether the hdev-lifetime fields are initialized
 * @board_id: BT board ID
 * @ver: BT version
 * @build_info: BT firmware build info string
 * @fw_dir: firmware directory configured by user via sysfs
 * @fw_logging: firmware logging configured by user via sysfs
 * @misc_flags: flags made from QBT_MISC_* bits defined above
 * @work_flags: flags made from QBT_WORK_* bits defined above
 * @dwork: delayed work to handle @work_flags
 * @md_ready: memdump functionality is ready
 * @md_state: devcoredump state as notified by the HCI devcoredump core
 * @md_submit_err: first error submitting to HCI devcoredump, 0 if none
 * @md_submit_size: total bytes submitted to HCI devcoredump, headers included
 * @md_pending_flags: QMD_FLAG_PENDING_* flags tracking per-subsystem memdump
 * @md: per-subsystem memdump collection track
 * @req_mutex: serializes sync requesters
 * @req_wait_q: wait queue for the requester
 * @req_spinlock: protects @req_state, @req, and @*req
 * @req_state: request state, see enum qhci_req_state
 * @req: the in-flight request, valid while @req_state is QHCI_REQ_PEND
 * @subsys: per-subsystem info for MSUBSYS chips
 */
struct btqcom_data {
	const char *drv_name;
	const char *btc_name;
	enum qbtc_category category;
	unsigned long flags;

	struct hci_dev *hdev;
	void *xport_data;

	bool inited;

	u16	board_id;
	struct qhci_btc_ver ver;
	char	build_info[160];

	const char *fw_dir;
	u8 fw_logging;

	unsigned long misc_flags;
	unsigned long		work_flags;
	struct delayed_work	dwork;

	bool md_ready;
	enum devcoredump_state md_state;
	int md_submit_err;
	u32 md_submit_size;
	unsigned long md_pending_flags;
	struct btqcom_memdump md;

	struct mutex		req_mutex;
	wait_queue_head_t	req_wait_q;
	spinlock_t		req_spinlock;
	int			req_state;
	struct qhci_req		*req;

	struct qbtc_subsys_data subsys[QBTC_SUBSYS_MAX];
};

/* BT host ID in PERI command/event/ACL */
#define QHCI_HOST_ID_BT 0

struct qperi_command_hdr {
	u8	host_id;
	__le16	opcode;
	u8	plen;
} __packed;
#define QPERI_COMMAND_HDR_SIZE (sizeof(struct qperi_command_hdr))

struct qperi_acl_hdr {
	u8	host_id;
	__le16	handle;
	__le16	dlen;
} __packed;
#define QPERI_ACL_HDR_SIZE	(sizeof(struct qperi_acl_hdr))
#define QPERI_MAX_FRAME_SIZE	(HCI_MAX_FRAME_SIZE + 1)

struct qperi_event_hdr {
	u8	host_id;
	u8	evt;
	u8	plen;
} __packed;
#define QPERI_EVENT_HDR_SIZE	(sizeof(struct qperi_event_hdr))
#define QPERI_MAX_EVENT_SIZE	(HCI_MAX_EVENT_SIZE + 1)

static inline struct qperi_event_hdr *qperi_event_header(const struct sk_buff *skb)
{
	return (struct qperi_event_hdr *)skb->data;
}

static inline struct qperi_acl_hdr *qperi_acl_header(const struct sk_buff *skb)
{
	return (struct qperi_acl_hdr *)skb->data;
}

static inline __u16 qperi_acl_handle(const struct sk_buff *skb)
{
	struct qperi_acl_hdr *hdr = qperi_acl_header(skb);

	return hci_handle(__le16_to_cpu(hdr->handle));
}

static inline __u16 qperi_acl_dlen(const struct sk_buff *skb)
{
	return __le16_to_cpu(qperi_acl_header(skb)->dlen);
}

static const char *qhci_pkt_type_name(u8 pkt_type)
{
	const char *pkt_type_name = "Unknown";

	switch (pkt_type) {
	case HCI_EVENT_PKT:
		pkt_type_name = "BT EVT";
		break;
	case QPERI_EVENT_PKT:
		pkt_type_name = "PERI EVT";
		break;
	case HCI_ACLDATA_PKT:
		pkt_type_name = "BT ACL";
		break;
	case QPERI_ACLDATA_PKT:
		pkt_type_name = "PERI ACL";
		break;
	default:
		break;
	}

	return pkt_type_name;
}

static void btusb_qcom_reset_sync(struct hci_dev *hdev);

/*
 * ============================================================================
 * Multi-subsys memdump design
 * ============================================================================
 */

/*
 * Design compatible with BT-only BTC as well:
 *
 * Abbreviations: SS - Subsystem, MD - Memdump, HDR - Header
 *
 * +--------------+-------------+---------+     +-------------+---------+
 * | file HDR     | SS_A MD HDR | SS_A    | ... | SS_N MD HDR | SS_N    |
 * | 512B         | 512B        | MD data |     | 512B        | MD data |
 * | by dmp_hdr() | appended as |         |     | appended as |         |
 * |              | MD data     |         |     | MD data     |         |
 * +--------------+-------------+---------+     +-------------+---------+
 *
 * 1) All SS MDs are collected into a SINGLE file.
 * 2) Each SS_X MD HDR records the size of its own MD data.
 * 3) On the 1st segment of the 1st SS: call hci_devcd_init() with
 *    dump_size — for a BT-only BTC, a 512B MD HDR plus the size carried
 *    in the segment itself; for a multi-subsys BTC, the sum of (a 512B
 *    MD HDR plus the max MD data size) over every SS. The file HDR is
 *    not counted, since the HCI devcoredump adds it on top of dump_size.
 * 4) On the 1st segment of every SS: call hci_devcd_append() to append
 *    its 512B MD HDR as ordinary MD data.
 * 5) On the last segment of a SS: call hci_devcd_complete() only if
 *    no other SS's MD is still pending.
 * 6) On the hardware error event (either PERI or BT), which always
 *    comes after all MDs have been reported by the BTC: call
 *    hci_devcd_complete() there if it has not been called yet, since a
 *    SS's MD is optional.
 */

static const u32 btqcom_memdump_maxsize[] = {
	[QHCI_SUBSYS_PERI]	= SZ_256K,
	[QHCI_SUBSYS_BT]	= SZ_1M,
	[QHCI_SUBSYS_UWB]	= 0,
	[QHCI_SUBSYS_TMEL]	= SZ_256K,
	[QHCI_SUBSYS_INVALID]	= SZ_1M,
};

/*
 * btqcom_memdump_hdr_room - helper to fill a memdump header up to 512 bytes
 * @skb: the devcoredump header skb to fill
 *
 * Return: bytes safe to write into @skb.
 */
static int btqcom_memdump_hdr_room(struct sk_buff *skb)
{
	int end_marker_len = sizeof(HCI_DEVCD_HDR_END_MARKER) - 1;
	/*
	 * don't use skb_tailroom(): observed to differ from requested
	 * alloc_size - skb->len
	 */
	int avail_room = HCI_DEVCD_HDR_SIZE_MAX - skb->len;
	u8 *ptr;

	/* 1 char + '\n' at least */
	if (avail_room >= end_marker_len + 2)
		return avail_room - end_marker_len;

	if (avail_room == end_marker_len)
		return 0;

	/* Avoid a blank line ('\n\n') in the header */
	skb_trim(skb, HCI_DEVCD_HDR_SIZE_MAX - end_marker_len - 1);
	ptr = skb_tail_pointer(skb) - 1;
	if (*ptr == '\n')
		*ptr = ' ';
	skb_put_u8(skb, '\n');

	return 0;
}

/*
 * btqcom_memdump_hdr - build the 512B memdump header
 * @hdev: the HCI device
 * @skb: the skb to build the header into
 *
 * Its 'Memdump Size' field is the size of the following payload.
 */
static void btqcom_memdump_hdr(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	int avail_room = btqcom_memdump_hdr_room(skb);
	struct btqcom_memdump *md = &qbt_data->md;
	struct qbtc_subsys_data *subsys_data;
	char buf[HCI_DEVCD_HDR_SIZE_MAX];
	struct qhci_btc_ver *btc_ver;
	const char *build_info;
	u16 board_id;
	int len = 0;

	if (!avail_room)
		return;

	len += scnprintf(buf + len, sizeof(buf) - len, "Header Type: memdump\n");
	len += scnprintf(buf + len, sizeof(buf) - len, "Header Size: %d\n",
			 HCI_DEVCD_HDR_SIZE_MAX);
	len += scnprintf(buf + len, sizeof(buf) - len, "Memdump Size: %u\n",
			 md->size);
	len += scnprintf(buf + len, sizeof(buf) - len, "Channel: %s\n",
			 qhci_pkt_type_name(md->from));
	len += scnprintf(buf + len, sizeof(buf) - len, "Owner: %s\n",
			 qhci_subsys_name(md->subsys));

	switch (md->subsys) {
	case QHCI_SUBSYS_PERI:
		subsys_data = &qbt_data->subsys[QBTC_SUBSYS_PERI];
		btc_ver = &subsys_data->ver;
		board_id = subsys_data->board_id;
		build_info = subsys_data->build_info;
		break;
	case QHCI_SUBSYS_TMEL:
		subsys_data = &qbt_data->subsys[QBTC_SUBSYS_TMEL];
		btc_ver = &subsys_data->ver;
		board_id = subsys_data->board_id;
		build_info = subsys_data->build_info;
		break;
	default:
		btc_ver = &qbt_data->ver;
		board_id = qbt_data->board_id;
		build_info = qbt_data->build_info;
		break;
	}

	len += scnprintf(buf + len, sizeof(buf) - len, "SoC Version: 0x%08x\n",
			 btc_ver->soc_ver);
	len += scnprintf(buf + len, sizeof(buf) - len, "ROM Version: 0x%04x\n",
			 btc_ver->rom_ver);
	len += scnprintf(buf + len, sizeof(buf) - len, "Patch Version: 0x%04x\n",
			 btc_ver->patch_ver);
	len += scnprintf(buf + len, sizeof(buf) - len, "Board ID: 0x%04x\n",
			 board_id);
	len += scnprintf(buf + len, sizeof(buf) - len, "Firmware Version: %s\n",
			 build_info);

	if (len > avail_room) {
		bt_dev_warn(hdev, "memdump header truncated (%d -> %d bytes)",
			    len, avail_room);
		len = avail_room;
		if (buf[len - 2] == '\n')
			buf[len - 2] = ' ';
		buf[len - 1] = '\n';
	}

	skb_put_data(skb, buf, len);
}

/*
 * btusb_qcom_memdump_hdr - build the 512B file header
 * @hdev: the HCI device
 * @skb: the skb to build the header into
 *
 * Implements the dmp_hdr_t for hci_devcd_register().
 */
static void btusb_qcom_memdump_hdr(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	char buf[HCI_DEVCD_HDR_SIZE_MAX];
	const char *vendor = "Qualcomm";
	int avail_room;
	int len = 0;

	avail_room = btqcom_memdump_hdr_room(skb);
	if (!avail_room)
		return;

	len += scnprintf(buf + len, sizeof(buf) - len, "Header Type: file\n");
	len += scnprintf(buf + len, sizeof(buf) - len, "Header Size: %d\n",
			 HCI_DEVCD_HDR_SIZE_MAX);
	len += scnprintf(buf + len, sizeof(buf) - len, "Driver: %s\n",
			 qbt_data->drv_name);
	len += scnprintf(buf + len, sizeof(buf) - len, "Vendor: %s\n", vendor);
	len += scnprintf(buf + len, sizeof(buf) - len, "Controller Name: %s\n",
			 qbt_data->btc_name);
	len += scnprintf(buf + len, sizeof(buf) - len, "Controller Category: %s\n",
			 qbtc_category_name(qbt_data->category));

	if (qbt_data->category == QBTC_CAT_MSUBSYS) {
		if (qbt_data->flags & QBT_FLAG_HAS_TMEL)
			len += scnprintf(buf + len, sizeof(buf) - len,
					 "Controller Subsys: PERI, TME-L\n");
		else
			len += scnprintf(buf + len, sizeof(buf) - len,
					 "Controller Subsys: PERI\n");
	}
	len += scnprintf(buf + len, sizeof(buf) - len, "VID: 0x%04x\n",
			 xport_data->idVendor);
	len += scnprintf(buf + len, sizeof(buf) - len, "PID: 0x%04x\n",
			 xport_data->idProduct);

	if (len > avail_room)
		bt_dev_warn(hdev, "dump header truncated (%d -> %d bytes)",
			    len, avail_room);

	memset(buf + len, 'P', sizeof(buf) - len);

	/*
	 * Deliberately overfill to skb's full 512 bytes, then leverage
	 * btqcom_memdump_hdr_room() to trim it back to the expected boundary.
	 */
	skb_put_data(skb, buf, HCI_DEVCD_HDR_SIZE_MAX - skb->len);
	btqcom_memdump_hdr_room(skb);
}

/* Used as the notify_change_t callback for hci_devcd_register(). */
static void btusb_qcom_notify_memdump(struct hci_dev *hdev, int state)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	enum devcoredump_state old_state;

	old_state = qbt_data->md_state;

	bt_dev_dbg(hdev, "memdump notify state: %s -> %s",
		   hci_devcd_state_name(old_state),
		   hci_devcd_state_name(state));
	bt_dev_dbg(hdev, "misc_flags: 0x%lx", READ_ONCE(qbt_data->misc_flags));
	bt_dev_dbg(hdev, "md_pending_flags: 0x%lx, md_submit_err: %d",
		   READ_ONCE(qbt_data->md_pending_flags),
		   READ_ONCE(qbt_data->md_submit_err));

	switch (state) {
	case HCI_DEVCOREDUMP_IDLE:
		break;
	case HCI_DEVCOREDUMP_ACTIVE:
		usb_autopm_get_interface_no_resume(xport_data->intf);
		bt_dev_dbg(hdev, "memdump notify: get autopm refcount");
		break;
	case HCI_DEVCOREDUMP_TIMEOUT:
		bt_dev_err(hdev, "memdump notify: %s", hci_devcd_state_name(state));
		qbt_data->md_submit_err = -ETIMEDOUT;
		qbt_data->md_pending_flags = 0;
		test_and_clear_bit(QBT_MISC_MEMDUMP_INCOMING,
				   &qbt_data->misc_flags);
		fallthrough;
	case HCI_DEVCOREDUMP_DONE:
	case HCI_DEVCOREDUMP_ABORT:
		usb_autopm_put_interface_no_suspend(xport_data->intf);
		bt_dev_dbg(hdev, "memdump notify: put autopm refcount");
		break;
	}

	qbt_data->md_state = state;

	if (state == HCI_DEVCOREDUMP_TIMEOUT)
		btusb_qcom_reset_sync(hdev);
}

/* handle received memdump frame here */

static inline void btqcom_reset_memdump(struct btqcom_memdump *md)
{
	memset(md, 0x00, sizeof(*md));
	md->subsys  = QHCI_SUBSYS_INVALID;
}

/*
 * btqcom_submit_memdump - submit one memdump segment to HCI devcoredump
 * @hdev: the HCI device the memdump comes from
 * @skb: the memdump segment payload
 *
 * See the "Multi-subsys memdump design" block above for the overall scheme
 * this implements.
 *
 * Return: 0 on success,
 *	1 if the memdump ended normally,
 *	a negative errno on failure.
 */
static int btqcom_submit_memdump(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct btqcom_memdump *md = &qbt_data->md;
	unsigned int dump_size, seg_len;
	bool is_first_subsys = false;
	int ret = 0;

	dump_size = 0;
	seg_len = skb->len;

	if (md->seqno == 0) {
		if (!test_and_set_bit(QBT_MISC_MEMDUMP_INCOMING,
				      &qbt_data->misc_flags)) {
			/* wake the waiter in btusb_do_cmd_timeout_work() */
			wake_up_var(&qbt_data->misc_flags);
			is_first_subsys = true;
			qbt_data->md_submit_err = 0;
			qbt_data->md_submit_size = 0;
			if (qbt_data->category == QBTC_CAT_MSUBSYS) {
				qbt_data->md_pending_flags = QMD_FLAG_PENDING_BT |
							     QMD_FLAG_PENDING_PERI;
				dump_size = HCI_DEVCD_HDR_SIZE_MAX +
					    btqcom_memdump_maxsize[QHCI_SUBSYS_BT] +
					    HCI_DEVCD_HDR_SIZE_MAX +
					    btqcom_memdump_maxsize[QHCI_SUBSYS_PERI];

				if (qbt_data->flags & QBT_FLAG_HAS_TMEL) {
					qbt_data->md_pending_flags |= QMD_FLAG_PENDING_TMEL;
					dump_size += HCI_DEVCD_HDR_SIZE_MAX +
						     btqcom_memdump_maxsize[QHCI_SUBSYS_TMEL];
				}
			} else {
				qbt_data->md_pending_flags = QMD_FLAG_PENDING_BT;
				dump_size = HCI_DEVCD_HDR_SIZE_MAX + md->size;
			}
		}

		bt_dev_info(hdev, "%s memdump incoming: %u bytes",
			    qhci_subsys_name(md->subsys), md->size);
		bt_dev_dbg(hdev, "md_pending_flags: 0x%lx",
			   qbt_data->md_pending_flags);

		if (is_first_subsys) {
			ret = hci_devcd_init(hdev, dump_size);
			if (ret) {
				kfree_skb(skb);
				bt_dev_err(hdev, "init memdump failed: %pe",
					   ERR_PTR(ret));
				return ret;
			}
			qbt_data->md_submit_size += HCI_DEVCD_HDR_SIZE_MAX;
			bt_dev_info(hdev, "file header: %u bytes",
				    HCI_DEVCD_HDR_SIZE_MAX);
		}

		if (!qbt_data->md_submit_err) {
			struct sk_buff *hdr_skb;

			hdr_skb = alloc_skb(HCI_DEVCD_HDR_SIZE_MAX, GFP_KERNEL);
			if (hdr_skb) {
				btqcom_memdump_hdr(hdev, hdr_skb);
				if (hdr_skb->len < HCI_DEVCD_HDR_SIZE_MAX)
					skb_put_zero(hdr_skb,
						     HCI_DEVCD_HDR_SIZE_MAX - hdr_skb->len);
				ret = hci_devcd_append(hdev, hdr_skb);
			} else {
				ret = -ENOMEM;
			}
			if (ret) {
				hci_devcd_abort(hdev);
				kfree_skb(skb);
				bt_dev_err(hdev, "append memdump header failed: %pe",
					   ERR_PTR(ret));
				return ret;
			}
			qbt_data->md_submit_size += HCI_DEVCD_HDR_SIZE_MAX;
			bt_dev_info(hdev, "%s memdump header appended: %u bytes",
				    qhci_subsys_name(md->subsys), HCI_DEVCD_HDR_SIZE_MAX);
		}
	}

	ret = qbt_data->md_submit_err;
	if (ret) {
		kfree_skb(skb);
	} else {
		ret = hci_devcd_append(hdev, skb);
		if (unlikely(ret)) {
			hci_devcd_abort(hdev);
			bt_dev_err(hdev, "append memdump failed: %pe", ERR_PTR(ret));
		} else {
			md->submit_size += seg_len;
			qbt_data->md_submit_size += seg_len;
		}
	}

	if (md->seqno != QHCI_MEMDUMP_SEQ_LAST)
		return ret;

	if (md->submit_size == md->size)
		bt_dev_info(hdev, "%s memdump fully collected",
			    qhci_subsys_name(md->subsys));
	else
		bt_dev_err(hdev, "%s memdump partially collected %u/%u bytes",
			   qhci_subsys_name(md->subsys), md->submit_size, md->size);

	qbt_data->md_pending_flags &= ~qmd_subsys_to_flag(md->subsys);
	if (qbt_data->md_pending_flags) {
		bt_dev_dbg(hdev, "md_pending_flags: 0x%lx",
			   qbt_data->md_pending_flags);
		return ret;
	}

	if (test_and_clear_bit(QBT_MISC_MEMDUMP_INCOMING, &qbt_data->misc_flags))
		bt_dev_dbg(hdev, "clear MEMDUMP_INCOMING on memdump completion");

	if (ret) {
		bt_dev_err(hdev, "memdump complete failed: %pe", ERR_PTR(ret));
		return ret;
	}

	ret = hci_devcd_complete(hdev);
	if (ret) {
		hci_devcd_abort(hdev);
		bt_dev_err(hdev, "memdump complete failed: %pe", ERR_PTR(ret));
	} else {
		ret = 1;
		bt_dev_info(hdev, "memdump completed: %u bytes",
			    qbt_data->md_submit_size);
	}
	return ret;
}

/*
 * btqcom_handle_memdump - handle one memdump segment
 * @hdev: the HCI device the @skb comes from
 * @skb: the memdump segment to handle
 *
 * Return: 0 on success,
 *	1 if the memdump ended normally,
 *	a negative errno on failure.
 */
static int btqcom_handle_memdump(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct btqcom_memdump *md = &qbt_data->md;
	const struct qhci_vse_memdump *md_vse;
	u8 pkt_type = hci_skb_pkt_type(skb);
	int subsys = QHCI_SUBSYS_INVALID;
	u16 seq_no = 0;
	u32 dump_size;
	int ret = 0;

	md_vse = skb_pull_data(skb, offsetof(struct qhci_vse_memdump, segdata));
	if (!md_vse) {
		ret = -EMSGSIZE;
		bt_dev_err(hdev, "bad memdump segment: too short (%u bytes)", skb->len);
		goto out_abort_md;
	}

	if (pkt_type == QPERI_EVENT_PKT || pkt_type == QPERI_ACLDATA_PKT) {
		subsys = md_vse->subsys;
		if (subsys >= QHCI_SUBSYS_MAX || !btqcom_memdump_maxsize[subsys] ||
		    (subsys == QHCI_SUBSYS_TMEL &&
		     !(qbt_data->flags & QBT_FLAG_HAS_TMEL))) {
			kfree_skb(skb);
			bt_dev_warn_ratelimited(hdev,
						"drop memdump of unsupported subsys %d",
						subsys);
			return 0;
		}
	}

	seq_no = le16_to_cpu(md_vse->seqno);
	if (seq_no == 0) {
		set_bit(qmd_subsys_to_bit(subsys), &qbt_data->misc_flags);

		if (!skb_pull(skb, QHCI_MEMDUMP_SEGDATA_GAP)) {
			ret = -EMSGSIZE;
			bt_dev_err(hdev, "bad first %s memdump segment: too short for dump_size",
				   qhci_subsys_name(subsys));
			goto out_abort_md;
		}

		dump_size = le32_to_cpu(md_vse->dump_size);
		if (!dump_size || dump_size > btqcom_memdump_maxsize[subsys]) {
			ret = -EILSEQ;
			bt_dev_err(hdev, "wrong %s memdump dump_size: %u",
				   qhci_subsys_name(subsys), dump_size);
			goto out_abort_md;
		}

		btqcom_reset_memdump(md);
		md->from   = pkt_type;
		md->subsys = subsys;
		md->size   = dump_size;
	} else {
		if (md->subsys != subsys) {
			ret = -EILSEQ;
			bt_dev_err_ratelimited(hdev,
					       "wrong memdump subsys: expected(%d), coming(%d)",
					       md->subsys, subsys);
			goto out_abort_md;
		}

		if (seq_no == QHCI_MEMDUMP_SEQ_LAST) {
			md->seqno = QHCI_MEMDUMP_SEQ_LAST;
		} else if (seq_no != md->seqno) {
			ret = -EILSEQ;
			bt_dev_err_ratelimited(hdev,
					       "wrong memdump seqno: expected(%u), coming(%u)",
					       md->seqno, seq_no);
			goto out_abort_md;
		}
	}

	/* @skb is consumed here */
	ret = btqcom_submit_memdump(hdev, skb);
	if (seq_no != QHCI_MEMDUMP_SEQ_LAST)
		md->seqno = seq_no + 1;

	goto out_reset_md;

out_abort_md:
	kfree_skb(skb);
	if (qbt_data->md_pending_flags && !qbt_data->md_submit_err) {
		hci_devcd_abort(hdev);
		bt_dev_err(hdev, "abort memdump");
	}

out_reset_md:
	if (ret < 0 && !qbt_data->md_submit_err)
		qbt_data->md_submit_err = ret;

	if (seq_no == QHCI_MEMDUMP_SEQ_LAST)
		btqcom_reset_memdump(md);

	return ret;
}

/*
 * ============================================================================
 * BT-HCI vendor-specific command/response
 * ============================================================================
 */

/* reserved BT ACL handle for enhanced logging */
#define QBT_HANDLE_ENHANCED_LOGGING 0xEDC
/* reserved BT ACL handle for memdump */
#define QBT_HANDLE_MEMDUMP 0xEDD

/*
 * Unless otherwise noted, for the VSCs below:
 * - command payload: struct qhci_cp_simple
 * - response payload: struct qhci_rp_generic
 *
 * Grouped by { opcode, sub_opcode, cp, rp }.
 */

/* BT EDL opcode and its sub-opcodes */
#define QBT_OP_EDL		0xFC00

#define BEDL_PATCH_GETVER	0x19
struct qbt_rp_patch_getver {
	u8	status;
	u8	sub_opcode;
	u8	dlen;
	__le32	product_id;
	__le16	patch_ver;
	__le16	rom_ver;
	__le32	soc_ver;
} __packed;

#define	BEDL_GET_BUILD_INFO 0x20
struct qbt_rp_get_build_info {
	u8	status;
	u8	sub_opcode;
	u8	dlen;
	u8	data[];
} __packed;

#define BEDL_GET_BOARD_ID	0x23
struct qbt_rp_get_board_id {
	u8	status;
	u8	sub_opcode;
	u8	dlen;
	__be16	board_id;
} __packed;

/* BT DEBUG opcode and its sub-opcodes */
#define QBT_OP_DEBUG		0xFC0C

#define BDBG_ERROR_FATAL_CMD	0x26
/* no response */

/* BT write BD_ADDR opcode, no sub-opcode */
#define QBT_OP_WRITE_BD_ADDR	0xFC14
struct qbt_cp_write_bd_addr {
	bdaddr_t bdaddr;
} __packed;

/* BT firmware logging opcode and its sub-opcodes */
#define QBT_OP_HOST_LOG		0xFC17

#define BHL_ENH_ENABLE_LOG	0x14
struct qbt_cp_config_fw_logging {
	u8	sub_opcode;
	u8	flags;
} __packed;

/*
 * QBT_CHECK_RP_GENERIC - sanity check rp @_skb against type @_rp_type
 * @_skb: the rp to check
 * @_rp_type: the type to interpret @_skb as
 * @_sub_opcode: sub_opcode to check @_skb against
 *
 * It is safe to evaluate @_skb and @_sub_opcode more than once for its
 * usages.
 *
 * To simplify logic, @_skb's length is checked first to keep later field
 * accesses safe.
 *
 * Returns a negative errno on failure, 0 on success, or error status code
 * otherwise.
 */
#define QBT_CHECK_RP_GENERIC(_skb, _rp_type, _sub_opcode)		\
({									\
	_rp_type *_rp_ptr;						\
	int _err = 0;							\
	do {								\
		if ((_skb)->len < sizeof(_rp_type)) {			\
			_err = -EBADMSG;				\
			break;						\
		}							\
		_rp_ptr = (void *)(_skb)->data;				\
		if (_rp_ptr->sub_opcode != (_sub_opcode)) {		\
			_err = -EILSEQ;					\
			break;						\
		}							\
		if (_rp_ptr->status) {					\
			_err = _rp_ptr->status;				\
			break;						\
		}							\
	} while (0);							\
	_err;								\
})

/* check the @dlen field on top of QBT_CHECK_RP_GENERIC() */
#define QBT_CHECK_RP_DLEN(_skb, _rp_type, _sub_opcode)			\
({									\
	_rp_type *_rp_ptr;						\
	int _err = 0;							\
	do {								\
		_err = QBT_CHECK_RP_GENERIC(_skb, _rp_type, _sub_opcode);\
		if (_err)						\
			break;						\
		_rp_ptr = (void *)(_skb)->data;				\
		if ((_skb)->len != offsetofend(_rp_type, dlen) +	\
				   _rp_ptr->dlen) {			\
			_err = -EMSGSIZE;				\
			break;						\
		}							\
	} while (0);							\
	_err;								\
})

/*
 * qbt_edl_patch_getver - read BTC version info
 * @hdev: the HCI device to query
 * @ver: output version info, filled on success
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qbt_edl_patch_getver(struct hci_dev *hdev, struct qhci_btc_ver *ver)
{
	const struct qhci_cp_simple cp = { .sub_opcode = BEDL_PATCH_GETVER };
	struct qbt_rp_patch_getver *rp;
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync_ev(hdev, QBT_OP_EDL, sizeof(cp),
				&cp, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		goto out;
	}

	err = QBT_CHECK_RP_DLEN(skb, struct qbt_rp_patch_getver, cp.sub_opcode);
	if (err)
		goto out_free_skb;

	rp = (void *)skb->data;
	ver->product_id = le32_to_cpu(rp->product_id);
	ver->soc_ver    = le32_to_cpu(rp->soc_ver);
	ver->rom_ver    = le16_to_cpu(rp->rom_ver);
	ver->patch_ver  = le16_to_cpu(rp->patch_ver);

	bt_dev_dbg(hdev, "Product ID   :0x%08x", ver->product_id);
	bt_dev_dbg(hdev, "SOC Version  :0x%08x", ver->soc_ver);
	bt_dev_dbg(hdev, "ROM Version  :0x%04x", ver->rom_ver);
	bt_dev_dbg(hdev, "Patch Version:0x%04x", ver->patch_ver);

	if (ver->soc_ver == 0 || ver->rom_ver == 0)
		err = -EILSEQ;

out_free_skb:
	kfree_skb(skb);

	if (err > 0) {
		bt_dev_dbg(hdev, "get BT version status error: 0x%02x",
			   err);
		err = -bt_to_errno(err);
	}
out:
	if (err)
		bt_dev_err(hdev, "get BT version failed: %pe", ERR_PTR(err));

	return err;
}

/*
 * qbt_edl_get_build_info - get BTC build info string
 * @hdev: the HCI device to query
 * @build_info: output build info string, allocated on success; caller frees
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qbt_edl_get_build_info(struct hci_dev *hdev, char **build_info)
{
	const struct qhci_cp_simple cp = { .sub_opcode = BEDL_GET_BUILD_INFO };
	struct qbt_rp_get_build_info *rp;
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync_ev(hdev, QBT_OP_EDL, sizeof(cp),
				&cp, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		goto out;
	}

	err = QBT_CHECK_RP_DLEN(skb, struct qbt_rp_get_build_info, cp.sub_opcode);
	if (err)
		goto out_free_skb;

	rp = (void *)skb->data;
	*build_info = kmemdup_nul(rp->data, rp->dlen, GFP_KERNEL);
	if (!*build_info) {
		err = -ENOMEM;
		goto out_free_skb;
	}

	bt_dev_dbg(hdev, "BT build info: %s", *build_info);

out_free_skb:
	kfree_skb(skb);

	if (err > 0) {
		bt_dev_err(hdev, "get BT build info status error: 0x%02x",
			   err);
		err = -bt_to_errno(err);
	}
out:
	if (err)
		bt_dev_err(hdev, "get BT build info failed: %pe", ERR_PTR(err));

	return err;
}

/*
 * qbt_edl_get_board_id - get BTC board ID
 * @hdev: the HCI device to query
 * @board_id: output board ID, filled on success
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qbt_edl_get_board_id(struct hci_dev *hdev, u16 *board_id)
{
	const struct qhci_cp_simple cp = { .sub_opcode = BEDL_GET_BOARD_ID };
	struct qbt_rp_get_board_id *rp;
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync_ev(hdev, QBT_OP_EDL, sizeof(cp),
				&cp, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		goto out;
	}

	err = QBT_CHECK_RP_DLEN(skb, struct qbt_rp_get_board_id, cp.sub_opcode);
	if (err)
		goto out_free_skb;

	rp = (void *)skb->data;
	*board_id = be16_to_cpu(rp->board_id);
	/* Take 0xffff as invalid board ID */
	if (*board_id == 0xffff)
		*board_id = 0;

	bt_dev_dbg(hdev, "BT Board ID: 0x%04x", *board_id);

out_free_skb:
	kfree_skb(skb);

	if (err > 0) {
		bt_dev_dbg(hdev, "get BT board ID status error: 0x%02x",
			   err);
		err = -bt_to_errno(err);
	}
out:
	if (err)
		bt_dev_err(hdev, "get BT board ID failed: %pe", ERR_PTR(err));

	return err;
}

/*
 * qbt_error_fatal_cmd - trigger a controller-side fatal error for debugging
 * @hdev: the HCI device to command
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qbt_error_fatal_cmd(struct hci_dev *hdev)
{
	const struct qhci_cp_simple cp = { .sub_opcode = BDBG_ERROR_FATAL_CMD };
	int err;

	err = __hci_cmd_send(hdev, QBT_OP_DEBUG, sizeof(cp), &cp);
	if (err < 0)
		bt_dev_err(hdev, "send error fatal cmd failed: %d", err);
	else
		bt_dev_info(hdev, "send error fatal cmd succeeded");

	return err;
}

/*
 * qbt_write_bda - set the controller's BD address
 * @hdev: the HCI device to configure
 * @bdaddr: the address to set
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qbt_write_bda(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct qbt_cp_write_bd_addr cp;
	int err;

	/* The controller expects the address in reversed byte order. */
	baswap(&cp.bdaddr, bdaddr);

	err = __hci_cmd_sync_status(hdev, QBT_OP_WRITE_BD_ADDR,
				    sizeof(cp), &cp, HCI_INIT_TIMEOUT);
	if (err < 0) {
		bt_dev_err(hdev, "set BT address failed: %pe",
			   ERR_PTR(err));
		return err;
	}
	if (err > 0) {
		bt_dev_err(hdev, "set BT address status error: 0x%02x",
			   err);
		return -bt_to_errno(err);
	}

	bt_dev_info(hdev, "BT address set to %pMR", bdaddr);

	return 0;
}

/*
 * qbt_config_fw_logging - configure enhanced firmware logging
 * @hdev: the HCI device to configure
 * @flags: logging configuration flags, normally 1 (enable) or 0 (disable)
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qbt_config_fw_logging(struct hci_dev *hdev, u8 flags)
{
	const struct qbt_cp_config_fw_logging cp = {
		.sub_opcode = BHL_ENH_ENABLE_LOG,
		.flags      = flags,
	};
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync_ev(hdev, QBT_OP_HOST_LOG, sizeof(cp),
				&cp, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		goto out;
	}

	err = QBT_CHECK_RP_GENERIC(skb, struct qhci_rp_generic, cp.sub_opcode);
	if (err)
		goto out_free_skb;

	bt_dev_dbg(hdev, "firmware logging configured (flags=0x%02x)", flags);

out_free_skb:
	kfree_skb(skb);

	if (err > 0) {
		bt_dev_dbg(hdev, "firmware logging config status error: 0x%02x", err);
		err = -bt_to_errno(err);
	}
out:
	if (err)
		bt_dev_err(hdev, "firmware logging config failed: %pe", ERR_PTR(err));

	return err;
}

/*
 * ============================================================================
 * qperi_handle_evt(): per-event handler and TX sync event sequence
 * ============================================================================
 */

#define QPERI_EV_CLASS_PERI	0xF0
#define QPERI_EV_TYPE_INVALID	0xff

/* Pass QPERI_EV_TYPE_INVALID as event if no events to sync */
#define DEFINE_QPERI_REQ_SPEC(specname, _opcode, _sub_opcode, _events...) \
	const struct qhci_req_spec specname = {			\
		.opcode     = (_opcode),				\
		.sub_opcode = (_sub_opcode),				\
		.event      = { _events, QPERI_EV_TYPE_INVALID },	\
	}

/*
 * struct qperi_evt - per-event helper for qperi_handle_evt()
 *
 * @handler: event handler, executed before the TX sync event check.
 *	Return: true if @skb was consumed, false otherwise.
 *
 * @verify_wakeup: extra verify if @skb belongs to the event sequence being synced.
 *
 *	Pull @skb empty if all its own checks pass and nothing is left for the
 *	requester to verify, and the requester will get NULL as response, e.g. a
 *	CSE, or a CCE whose payload has only status, or only status and
 *	sub_opcode.
 *
 *	Return: 0 if @skb doesn't belong, 1 if it does, or a negative errno if
 *		@skb is malformed.
 */
struct qperi_evt {
	bool (*handler)(struct hci_dev *hdev, struct sk_buff *skb);
	int (*verify_wakeup)(struct hci_dev *hdev, struct qhci_req *req,
			     struct sk_buff *skb);
};

#define PERI_EV_CMD_STATUS	0x00
struct peri_ev_cmd_status {
	u8	status;
	u8	ncmd;
	__le16	opcode;
} __packed;

#define PERI_EV_CMD_COMPLETE	0x01
struct peri_ev_cmd_complete {
	u8	ncmd;
	__le16	opcode;
} __packed;

#define PERI_EV_SUBSYS_ACTIVATE_COMPLETE	0x02
struct peri_ev_subsys_activate_complete {
	u8	subsys;
	u8	action;
} __packed;

#define PERI_EV_SUBSYS_PATCH_NOTIFICATION	0x03
struct peri_ev_subsys_patch_notification {
	u8	status;
	u8	subsys;
} __packed;

#define PERI_EV_CRASH_DUMP_MEMDUMP 0x04
/* struct qhci_vse_memdump */

#define PERI_EV_HARDWARE_ERROR	0x06
struct peri_ev_hardware_error {
	u8	code;
} __packed;

/* As qperi_evt::verify_wakeup() for PERI_EV_CMD_STATUS */
static int peri_verify_cmd_status(struct hci_dev *hdev __maybe_unused,
				  struct qhci_req *req, struct sk_buff *skb)
{
	const struct peri_ev_cmd_status *ev;

	if (skb->len != sizeof(*ev))
		return -EMSGSIZE;

	ev = (const void *)skb->data;

	if (le16_to_cpu(ev->opcode) != req->spec->opcode)
		return 0;

	req->req_result = ev->status;
	if (!req->req_result)
		skb_pull(skb, sizeof(*ev));

	return 1;
}

/* CCE rp payload: [0] status, [1] sub_opcode if present, then rp-specific data if any */
static int peri_verify_rp_payload(struct hci_dev *hdev __maybe_unused,
				  struct qhci_req *req, struct sk_buff *skb)
{
	if (!skb->len)
		return -EMSGSIZE;

	if (req->spec->sub_opcode != QHCI_SUB_OPCODE_INVALID) {
		if (skb->len < 2)
			return -EMSGSIZE;

		if (skb->data[1] != req->spec->sub_opcode)
			return 0;
	}

	req->req_result = skb->data[0];
	if (!req->req_result && skb->len <= 2)
		skb_pull(skb, skb->len);

	return 1;
}

/* As qperi_evt::verify_wakeup() for PERI_EV_CMD_COMPLETE */
static int peri_verify_cmd_complete(struct hci_dev *hdev, struct qhci_req *req,
				    struct sk_buff *skb)
{
	const struct peri_ev_cmd_complete *ev;
	u16 opcode;

	ev = skb_pull_data(skb, sizeof(*ev));
	if (!ev)
		return -EMSGSIZE;

	opcode = le16_to_cpu(ev->opcode);
	if (opcode != req->spec->opcode)
		return 0;

	hci_skb_opcode(skb) = opcode;

	return peri_verify_rp_payload(hdev, req, skb);
}

/* As qperi_evt::verify_wakeup() for PERI_EV_SUBSYS_ACTIVATE_COMPLETE */
static int peri_verify_subsys_activate_complete(struct hci_dev *hdev __maybe_unused,
						struct qhci_req *req __maybe_unused,
						struct sk_buff *skb)
{
	if (skb->len != sizeof(struct peri_ev_subsys_activate_complete))
		return -EMSGSIZE;

	return 1;
}

/* As qperi_evt::verify_wakeup() for PERI_EV_SUBSYS_PATCH_NOTIFICATION */
static int peri_verify_subsys_patch_notification(struct hci_dev *hdev __maybe_unused,
						 struct qhci_req *req,
						 struct sk_buff *skb)
{
	const struct peri_ev_subsys_patch_notification *ev;

	if (skb->len != sizeof(*ev))
		return -EMSGSIZE;

	ev = (const void *)skb->data;
	req->req_result = ev->status;

	return 1;
}

/* As qperi_evt::handler() for PERI_EV_CRASH_DUMP_MEMDUMP */
static bool peri_crash_dump_memdump(struct hci_dev *hdev, struct sk_buff *skb)
{
	btqcom_handle_memdump(hdev, skb);

	return true;
}

/* As qperi_evt::handler() for PERI_EV_HARDWARE_ERROR */
static bool peri_hardware_error(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	const struct peri_ev_hardware_error *ev;
	u8 code = HCI_ERROR_UNSPECIFIED;

	ev = skb_pull_data(skb, sizeof(*ev));
	if (ev)
		code = ev->code;
	else
		bt_dev_err(hdev, "malformed PERI hardware error event");

	set_bit(QBT_MISC_HWERR_PERI, &qbt_data->misc_flags);
	__hci_reset_dev(hdev, code);

	return false;
}

static const struct qperi_evt qperi_evt_table[] = {
	/* [0x00 = PERI_EV_CMD_STATUS] */
	[PERI_EV_CMD_STATUS] = {
		.verify_wakeup = peri_verify_cmd_status,
	},
	/* [0x01 = PERI_EV_CMD_COMPLETE] */
	[PERI_EV_CMD_COMPLETE] = {
		.verify_wakeup = peri_verify_cmd_complete,
	},
	/* [0x02 = PERI_EV_SUBSYS_ACTIVATE_COMPLETE] */
	[PERI_EV_SUBSYS_ACTIVATE_COMPLETE] = {
		.verify_wakeup = peri_verify_subsys_activate_complete,
	},
	/* [0x03 = PERI_EV_SUBSYS_PATCH_NOTIFICATION] */
	[PERI_EV_SUBSYS_PATCH_NOTIFICATION] = {
		.verify_wakeup = peri_verify_subsys_patch_notification,
	},
	/* [0x04 = PERI_EV_CRASH_DUMP_MEMDUMP] */
	[PERI_EV_CRASH_DUMP_MEMDUMP] = {
		.handler = peri_crash_dump_memdump,
	},
	/* [0x06 = PERI_EV_HARDWARE_ERROR] */
	[PERI_EV_HARDWARE_ERROR] = {
		.handler = peri_hardware_error,
	},
};

/*
 * qperi_try_wakeup - check if event @skb with type @ev_type can wake up the requester
 * @hdev: the HCI device the event comes from
 * @ev_type: the event type
 * @skb: the event payload, or NULL on skb clone failure
 *
 * Note: the caller only cares about the return value rather than the wakeup result.
 *
 * Return: true if @skb was consumed, false otherwise.
 */
static bool qperi_try_wakeup(struct hci_dev *hdev, u8 ev_type,
			     struct sk_buff *skb)
{
	const struct qperi_evt *evt = &qperi_evt_table[ev_type];
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct sk_buff *req_rsp = skb;
	struct qhci_req *req;
	int res;

	/* fast path */
	if (READ_ONCE(qbt_data->req_state) != QHCI_REQ_PEND)
		return false;

	guard(spinlock)(&qbt_data->req_spinlock);

	if (qbt_data->req_state != QHCI_REQ_PEND)
		return false;

	req = qbt_data->req;
	if (WARN_ON_ONCE(!req) || WARN_ON_ONCE(!req->spec))
		return false;

	if (req->spec->event[req->event_idx] != ev_type)
		return false;

	/* skb clone failure */
	if (!skb) {
		req->req_result = -ENOMEM;
		goto out_wakeup;
	}

	/* malformed frame */
	if (!skb->len) {
		req->req_result = -EMSGSIZE;
		goto out_wakeup;
	}

	req->req_result = 0;
	if (evt->verify_wakeup) {
		res = evt->verify_wakeup(hdev, req, skb);
		if (!res)
			return false;

		if (res < 0) {
			req->req_result = res;
			goto out_wakeup;
		}

		/* HCI status error */
		if (req->req_result)
			goto out_wakeup;

		/* nothing left for the requester to verify */
		if (!req_rsp->len)
			req_rsp = NULL;
	}

	if (req->spec->event[req->event_idx + 1] != QPERI_EV_TYPE_INVALID) {
		req->event_idx++;
		return false;
	}

out_wakeup:
	if (req->req_result < 0)
		req_rsp = NULL;

	req->req_rsp = req_rsp;
	if (req->req_result)
		bt_dev_err(hdev, "PERI cmd opcode(0x%04x) sub_opcode(0x%02x) failed on ev_type(0x%02x): %d",
			   req->spec->opcode, req->spec->sub_opcode, ev_type, req->req_result);
	qbt_data->req_state = QHCI_REQ_DONE;
	wake_up_interruptible(&qbt_data->req_wait_q);
	/* test if the requester now owns @skb */
	return req_rsp == skb;
}

/*
 * __qperi_tx_sync_evt - send a frame and sync its event sequence
 * @hdev: the HCI device
 * @iter: the frame to send, NULL to sync only
 * @spec: the request spec to sync, NULL to send only
 * @timeout: timeout in jiffies for the sync
 * @evt_idx: index of the waking event
 * @status: HCI status code if it has one, 0 otherwise
 *
 * Requires the caller holds qbt_data->req_mutex.
 *
 * Return: ERR_PTR() on failure,
 *	NULL on success for send-only or nothing left for the caller to verify,
 *	the waking event otherwise.
 */
static struct sk_buff *__qperi_tx_sync_evt(struct hci_dev *hdev,
					   struct iov_iter *iter,
					   const struct qhci_req_spec *spec,
					   unsigned int timeout,
					   u8 *evt_idx, u8 *status)
{
	struct qhci_req req = { .pkt_type = QPERI_EVENT_PKT, .spec = spec };
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct sk_buff *ret = NULL;
	int res = 0, wait_res = 0;
	int req_state;

	if (!iter && !spec)
		return ERR_PTR(-EINVAL);

	/* send only, no event to sync */
	if (!spec) {
		res = hci_send_vendor_frame(hdev, iter);
		if (res < 0) {
			bt_dev_err(hdev, "send-only PERI frame failed: %pe", ERR_PTR(res));
			return ERR_PTR(res);
		}
		return NULL;
	}

	scoped_guard(spinlock, &qbt_data->req_spinlock) {
		qbt_data->req = &req;
		qbt_data->req_state = QHCI_REQ_PEND;
	}

	if (iter)
		res = hci_send_vendor_frame(hdev, iter);

	if (!res)
		wait_res = wait_event_interruptible_timeout(qbt_data->req_wait_q,
							    READ_ONCE(qbt_data->req_state) !=
							    QHCI_REQ_PEND,
							    timeout);

	scoped_guard(spinlock, &qbt_data->req_spinlock) {
		req_state = qbt_data->req_state;
		qbt_data->req = NULL;
		qbt_data->req_state = QHCI_REQ_DONE;
	}

	if (res >= 0) {
		switch (req_state) {
		case QHCI_REQ_DONE:
			res = req.req_result;
			break;
		case QHCI_REQ_CANCELED:
			if (req.req_result < 0)
				bt_dev_info(hdev, "PERI cmd opcode(0x%04x) sub_opcode(0x%02x) canceled: %pe",
					    spec->opcode, spec->sub_opcode,
					    ERR_PTR(req.req_result));
			else
				bt_dev_info(hdev, "PERI cmd opcode(0x%04x) sub_opcode(0x%02x) canceled",
					    spec->opcode, spec->sub_opcode);
			res = req.req_result < 0 ? req.req_result : -ECANCELED;
			break;
		case QHCI_REQ_PEND:
		default:
			res = wait_res < 0 ? -EINTR : -ETIMEDOUT;
			break;
		}
	}

	ret = req.req_rsp;
	if (res < 0) {
		/* log unless qperi_try_wakeup() already did */
		if (res != req.req_result)
			bt_dev_err(hdev, "PERI cmd opcode(0x%04x) sub_opcode(0x%02x) failed: %pe",
				   spec->opcode, spec->sub_opcode, ERR_PTR(res));

		if (WARN_ON_ONCE(req.req_rsp))
			kfree_skb(req.req_rsp);

		ret = ERR_PTR(res);
	}

	if (evt_idx)
		*evt_idx = req.event_idx;
	if (status)
		*status = (u8)req.req_result;
	return ret;
}

/* synchronously cancel pending request with error code @err */
static void qperi_tx_sync_cancel_sync(struct hci_dev *hdev, int err)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct qhci_req *req;

	guard(spinlock)(&qbt_data->req_spinlock);

	if (qbt_data->req_state != QHCI_REQ_PEND)
		return;

	req = qbt_data->req;
	if (WARN_ON_ONCE(!req))
		return;

	req->req_result = err;
	qbt_data->req = NULL;
	qbt_data->req_state = QHCI_REQ_CANCELED;

	wake_up_interruptible(&qbt_data->req_wait_q);
}

/*
 * __qperi_cmd_sync_evt - send a command and sync its event sequence
 * @hdev: the HCI device
 * @opcode: command opcode, QHCI_OPCODE_INVALID to sync only
 * @plen: length of @cp
 * @cp: the command payload
 * @spec: the request spec to sync, NULL to send only
 * @evt_idx: index of the waking event
 * @status: HCI status code if it has one, 0 otherwise
 *
 * Requires the caller holds qbt_data->req_mutex.
 *
 * Return: ERR_PTR() on failure,
 *	NULL on success for send-only or nothing left for the caller to verify,
 *	the waking event otherwise.
 */
static struct sk_buff *__qperi_cmd_sync_evt(struct hci_dev *hdev,
					    u16 opcode, u32 plen, const void *cp,
					    const struct qhci_req_spec *spec,
					    u8 *evt_idx, u8 *status)
{
	u8 pkt_type = QPERI_COMMAND_PKT;
	struct qperi_command_hdr cmd_hdr = {
		.host_id = QHCI_HOST_ID_BT,
		.opcode  = cpu_to_le16(opcode),
		.plen    = (u8)plen,
	};
	struct kvec cmd_kv[] = {
		{ .iov_base = &pkt_type,       .iov_len = 1 },
		{ .iov_base = &cmd_hdr,        .iov_len = sizeof(cmd_hdr) },
		{ .iov_base = (void *)cp,      .iov_len = plen },
	};
	struct iov_iter iter;

	/* no command to send, sync only */
	if (opcode == QHCI_OPCODE_INVALID)
		return __qperi_tx_sync_evt(hdev, NULL, spec, HCI_INIT_TIMEOUT, evt_idx, status);

	iov_iter_kvec(&iter, ITER_SOURCE, cmd_kv, plen ? 3 : 2,
		      1 + sizeof(cmd_hdr) + plen);

	return __qperi_tx_sync_evt(hdev, &iter, spec, HCI_INIT_TIMEOUT, evt_idx, status);
}

static inline struct sk_buff *qperi_cmd_sync_evt(struct hci_dev *hdev,
						 u16 opcode, u32 plen, const void *cp,
						 const struct qhci_req_spec *spec,
						 u8 *evt_idx, u8 *status)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);

	guard(mutex)(&qbt_data->req_mutex);

	return __qperi_cmd_sync_evt(hdev, opcode, plen, cp, spec, evt_idx, status);
}

/*
 * send a command and sync an event with type @ev_type;
 * @cp's first byte is sub_opcode for all commands so far
 *
 * Requires the caller holds qbt_data->req_mutex.
 */
static struct sk_buff *__qperi_cmd_sync_one_evt(struct hci_dev *hdev,
						u16 opcode, u32 plen, const void *cp,
						u8 ev_type, u8 *status)
{
	u8 sub_opcode = plen ? *(const u8 *)cp : QHCI_SUB_OPCODE_INVALID;
	DEFINE_QPERI_REQ_SPEC(spec, opcode, sub_opcode, ev_type);

	return __qperi_cmd_sync_evt(hdev, opcode, plen, cp, &spec, NULL, status);
}

static inline struct sk_buff *qperi_cmd_sync_one_evt(struct hci_dev *hdev,
						     u16 opcode, u32 plen, const void *cp,
						     u8 ev_type, u8 *status)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);

	guard(mutex)(&qbt_data->req_mutex);

	return __qperi_cmd_sync_one_evt(hdev, opcode, plen, cp, ev_type, status);
}

/*
 * only wait for an unsolicited event with type @ev_type
 *
 * Requires the caller holds qbt_data->req_mutex.
 */
static struct sk_buff __maybe_unused *__qperi_wait_one_evt(struct hci_dev *hdev,
							   u8 ev_type, u8 *status)
{
	DEFINE_QPERI_REQ_SPEC(spec, QHCI_OPCODE_INVALID, QHCI_SUB_OPCODE_INVALID, ev_type);

	return __qperi_tx_sync_evt(hdev, NULL, &spec, HCI_INIT_TIMEOUT, NULL, status);
}

/*
 * handle a received PERI event @skb, called from hdev->recv_vendor_pkt();
 * generic processing first, then the TX sync event, without interfering
 */
static void qperi_handle_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	const struct qhci_vse_comm *vse_comm;
	const struct qperi_event_hdr *hdr;
	const struct qperi_evt *evt_entry;
	/* for qperi_try_wakeup() call below */
	struct sk_buff *orig_skb = NULL;
	u8 ev_type;
	int res;

	ev_type = QPERI_EV_TYPE_INVALID;
	res = -EBADMSG;
	hdr = skb_pull_data(skb, QPERI_EVENT_HDR_SIZE);
	if (!hdr || hdr->evt != HCI_EV_VENDOR)
		goto out_free_skb;

	res = -EILSEQ;
	vse_comm = skb_pull_data(skb, QHCI_VSE_COMM_SIZE);
	if (!vse_comm || vse_comm->ev_class != QPERI_EV_CLASS_PERI)
		goto out_free_skb;

	ev_type = vse_comm->ev_type;
	res = -ENOENT;
	if (ev_type >= ARRAY_SIZE(qperi_evt_table))
		goto out_free_skb;

	res = 0;
	hci_skb_event(skb) = ev_type;
	evt_entry = &qperi_evt_table[ev_type];

	if (evt_entry->handler) {
		const struct qhci_req *req;
		bool may_wakeup;

		spin_lock(&qbt_data->req_spinlock);
		req = qbt_data->req;
		may_wakeup = qbt_data->req_state == QHCI_REQ_PEND &&
			req && req->spec &&
			req->spec->event[req->event_idx] == ev_type;
		spin_unlock(&qbt_data->req_spinlock);

		if (may_wakeup) {
			orig_skb = skb_clone(skb, GFP_KERNEL);
			if (!orig_skb)
				res = -ENOMEM;
		}

		if (evt_entry->handler(hdev, skb))
			skb = NULL;
	} else {
		orig_skb = skb;
		skb = NULL;
	}

	if (qperi_try_wakeup(hdev, ev_type, orig_skb))
		orig_skb = NULL;

out_free_skb:
	kfree_skb(orig_skb);
	kfree_skb(skb);
	if (res < 0)
		bt_dev_err(hdev, "fails to handle PERI event with type 0x%02x: %pe",
			   ev_type, ERR_PTR(res));
}

/*
 * ============================================================================
 * PERI-HCI vendor-specific command/response
 * ============================================================================
 */

/* reserved PERI ACL handle for enhanced logging */
#define QPERI_HANDLE_ENHANCED_LOGGING 0xEC0
/* reserved PERI ACL handle for memdump */
#define QPERI_HANDLE_MEMDUMP 0xEC1

/*
 * Unless otherwise noted, for the VSCs below:
 * - command payload: struct qhci_cp_simple
 * - response payload: struct qhci_rp_generic
 *
 * Grouped by { opcode, sub_opcode, cp, rp }.
 */

/* PERI EDL opcode and its sub-opcodes */
#define QPERI_OP_EDL		0xFFF0

#define PEDL_GET_BUILD_INFO	0x09
struct qperi_cp_generic {
	u8	sub_opcode;
	u8	subsys;
} __packed;
struct qperi_rp_get_build_info {
	u8	status;
	u8	sub_opcode;
	u8	subsys;
	u8	dlen;
	u8	data[];
} __packed;

/* PERI Generic opcode and its sub-opcodes */
#define QPERI_OP_GENERIC	0xFFF1

#define PGEN_PERI_RESET		0x03
/* struct qhci_rp_generic */
/* struct peri_ev_subsys_patch_notification */

#define PGEN_INITIATE_BT_CRASH	0x05
/* struct peri_ev_cmd_status */

#define __QPERI_CP_INIT_SUBSYS_0(...)
#define __QPERI_CP_INIT_SUBSYS_1(_subsys, ...)	.subsys = (_subsys),

#define QPERI_CP_INITIALIZER(_sub_opcode, _subsys...) {			\
	.sub_opcode = (_sub_opcode),					\
	CONCATENATE(__QPERI_CP_INIT_SUBSYS_, COUNT_ARGS(_subsys))(_subsys)	\
}

/*
 * QPERI_CHECK_RP_LEN - check rp @_skb's length and its @dlen
 * @_skb: the rp to check
 * @_rp_type: the type to interpret @_skb as
 *
 * It is safe to evaluate @_skb more than once for its usages.
 *
 * Returns a negative errno on failure, or 0 on success.
 */
#define QPERI_CHECK_RP_LEN(_skb, _rp_type)				\
({									\
	_rp_type *_rp_ptr;						\
	int _err = 0;							\
	do {								\
		if (!(_skb) || (_skb)->len < sizeof(_rp_type)) {	\
			_err = -EBADMSG;				\
			break;						\
		}							\
		_rp_ptr = (void *)(_skb)->data;				\
		if ((_skb)->len != offsetofend(_rp_type, dlen) +	\
				   _rp_ptr->dlen) {			\
			_err = -EMSGSIZE;				\
			break;						\
		}							\
	} while (0);							\
	_err;								\
})

/*
 * qperi_get_subsys_build_info - get subsystem build info string
 * @hdev: the HCI device to query
 * @subsys: the subsystem to query
 * @build_info: output build info string, allocated on success; caller frees
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qperi_get_subsys_build_info(struct hci_dev *hdev, u8 subsys,
				       char **build_info)
{
	struct qperi_cp_generic cp = QPERI_CP_INITIALIZER(PEDL_GET_BUILD_INFO, subsys);
	struct qperi_rp_get_build_info *rp;
	struct sk_buff *skb;
	u8 status = 0;
	int ret;

	skb = qperi_cmd_sync_one_evt(hdev, QPERI_OP_EDL, sizeof(cp), &cp,
				     PERI_EV_CMD_COMPLETE, &status);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	if (status) {
		ret = -bt_to_errno(status);
		goto out_free_skb;
	}

	ret = QPERI_CHECK_RP_LEN(skb, struct qperi_rp_get_build_info);
	if (ret)
		goto out_free_skb;
	rp = (void *)skb->data;

	if (rp->subsys != subsys) {
		ret = -EILSEQ;
		goto out_free_skb;
	}

	*build_info = kmemdup_nul(rp->data, rp->dlen, GFP_KERNEL);
	ret = *build_info ? 0 : -ENOMEM;

out_free_skb:
	kfree_skb(skb);

	if (ret)
		bt_dev_err(hdev, "get %s build info failed: %pe",
			   qhci_subsys_name(subsys), ERR_PTR(ret));
	else
		bt_dev_dbg(hdev, "%s build info: %s",
			   qhci_subsys_name(subsys), *build_info);

	return ret;
}

/*
 * qperi_hci_reset - reset PERI HCI
 * @hdev: the HCI device to reset
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qperi_hci_reset(struct hci_dev *hdev)
{
	DEFINE_QPERI_REQ_SPEC(spec, QPERI_OP_GENERIC, PGEN_PERI_RESET,
			      PERI_EV_CMD_COMPLETE, PERI_EV_SUBSYS_PATCH_NOTIFICATION);
	struct qhci_cp_simple cp = QPERI_CP_INITIALIZER(PGEN_PERI_RESET);
	struct sk_buff *skb;
	u8 evt_idx = 0;
	u8 status = 0;
	int ret = 0;

	skb = qperi_cmd_sync_evt(hdev, QPERI_OP_GENERIC, sizeof(cp), &cp,
				 &spec, &evt_idx, &status);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	if (status) {
		ret = -bt_to_errno(status);
	} else if (evt_idx == 1) {
		const struct peri_ev_subsys_patch_notification *ev;

		ev = skb_pull_data(skb, sizeof(*ev));
		if (ev->subsys != QHCI_SUBSYS_PERI) {
			ret = -EILSEQ;
			bt_dev_err(hdev, "PERI HCI reset via QHCI failed: wrong subsys %d",
				   ev->subsys);
		}
	}

	kfree_skb(skb);

	if (!ret)
		bt_dev_info(hdev, "PERI HCI reset via QHCI succeeded");

	return ret;
}

/*
 * qperi_initiate_bt_crash - initiate a BT subsystem crash
 * @hdev: the HCI device
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int qperi_initiate_bt_crash(struct hci_dev *hdev)
{
	struct qhci_cp_simple cp = QPERI_CP_INITIALIZER(PGEN_INITIATE_BT_CRASH);
	struct sk_buff *skb;
	u8 status = 0;
	int ret = 0;

	skb = qperi_cmd_sync_one_evt(hdev, QPERI_OP_GENERIC, sizeof(cp), &cp,
				     PERI_EV_CMD_STATUS, &status);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	if (skb) {
		ret = -bt_to_errno(status);
		kfree_skb(skb);
	}

	if (!ret)
		bt_dev_info(hdev, "PERI initiate BT crash succeeded");

	return ret;
}

/*
 * ============================================================================
 * hdev callbacks
 * ============================================================================
 */

/* test if a HW or SW reset is available */
static inline bool btqcom_has_btc_reset(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);

	return qbt_data->flags & (QBT_FLAG_HW_RESET | QBT_FLAG_SW_RESET);
}

/*
 * btqcom_set_bdaddr - set BD_ADDR
 * @hdev: the HCI device
 * @bdaddr: the BD_ADDR to set
 *
 * Implements hdev->set_bdaddr().
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btqcom_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	int ret;

	ret = __hci_reset_sync(hdev);
	if (ret) {
		bt_dev_err(hdev, "HCI reset before setting BD_ADDR failed: %pe",
			   ERR_PTR(ret));
		return ret;
	}

	ret = qbt_write_bda(hdev, bdaddr);

	return ret;
}

/*
 * btqcom_handle_ev_vendor - handle @skb if interested
 * @hdev: the HCI device @skb comes from
 * @skb: the VSE payload
 *
 * Implements hdev->handle_ev_vendor().
 *
 * Return: true if @skb was handled, false otherwise.
 */
static bool btqcom_handle_ev_vendor(struct hci_dev *hdev, struct sk_buff *skb)
{
	const struct qhci_vse_comm *vse_comm;
	u16 vse_id;

	if (skb->len < QHCI_VSE_COMM_SIZE)
		return false;

	vse_comm = (const void *)skb->data;
	vse_id = vse_comm->ev_class << 8 | vse_comm->ev_type;

	switch (vse_id) {
	case (QBT_EV_CLASS_DATALOG << 8) | QBT_EV_TYPE_MEMDUMP:
		skb_pull_data(skb, QHCI_VSE_COMM_SIZE);
		btqcom_handle_memdump(hdev, skb_get(skb));
		return true;
	default:
		return false;
	}
}

/* handle a received BT vendor ACL frame */
static void handle_acl_vendor(struct hci_dev *hdev, struct sk_buff *skb)
{
	const struct qhci_vse_comm *vse_comm;
	const struct hci_event_hdr *evt_hdr;
	u16 vse_id;

	if (hci_acl_handle(skb) != QBT_HANDLE_MEMDUMP)
		goto out_free_skb;

	skb_pull(skb, HCI_ACL_HDR_SIZE);

	evt_hdr = skb_pull_data(skb, HCI_EVENT_HDR_SIZE);
	if (!evt_hdr || evt_hdr->evt != HCI_EV_VENDOR)
		goto out_free_skb;

	vse_comm = skb_pull_data(skb, QHCI_VSE_COMM_SIZE);
	if (!vse_comm)
		goto out_free_skb;

	vse_id = vse_comm->ev_class << 8 | vse_comm->ev_type;

	switch (vse_id) {
	case (QBT_EV_CLASS_DATALOG << 8) | QBT_EV_TYPE_MEMDUMP:
		btqcom_handle_memdump(hdev, skb);
		return;
	default:
		break;
	}

out_free_skb:
	kfree_skb(skb);
}

/* handle a received PERI ACL frame */
static void qperi_handle_acl(struct hci_dev *hdev, struct sk_buff *skb)
{
	const struct hci_event_hdr *bt_evt_hdr;
	const struct qhci_vse_comm *vse_comm;
	u8 ev_type;

	if (qperi_acl_handle(skb) != QPERI_HANDLE_MEMDUMP)
		goto out_free_skb;

	skb_pull(skb, QPERI_ACL_HDR_SIZE);

	bt_evt_hdr = skb_pull_data(skb, HCI_EVENT_HDR_SIZE);
	if (!bt_evt_hdr || bt_evt_hdr->evt != HCI_EV_VENDOR)
		goto out_free_skb;

	vse_comm = skb_pull_data(skb, sizeof(*vse_comm));
	if (!vse_comm || vse_comm->ev_class != QPERI_EV_CLASS_PERI)
		goto out_free_skb;

	ev_type = vse_comm->ev_type;
	switch (ev_type) {
	case PERI_EV_CRASH_DUMP_MEMDUMP:
		btqcom_handle_memdump(hdev, skb);
		return;
	default:
		break;
	}

out_free_skb:
	kfree_skb(skb);
}

/* Implements hdev->recv_vendor_pkt(). */
static void btqcom_recv_vendor_pkt(struct hci_dev *hdev, struct sk_buff *skb)
{
	hci_skb_pkt_type(skb) = *(const u8 *)skb_pull_data(skb, 1);

	switch (hci_skb_pkt_type(skb)) {
	case QPERI_EVENT_PKT:
		qperi_handle_evt(hdev, skb);
		break;
	case QPERI_ACLDATA_PKT:
		qperi_handle_acl(hdev, skb);
		break;
	case HCI_ACLDATA_PKT:
		handle_acl_vendor(hdev, skb);
		break;
	default:
		bt_dev_err(hdev, "unexpected vendor HCI frame with pkt_type 0x%02x",
			   hci_skb_pkt_type(skb));
		kfree_skb(skb);
		break;
	}
}

/* Implements hdev->dump.coredump() for hci_devcd_register(). */
static void btusb_qcom_trigger_memdump(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);

	if (qbt_data->category == QBTC_CAT_MSUBSYS)
		qperi_initiate_bt_crash(hdev);
	else
		qbt_error_fatal_cmd(hdev);
}

/* return 0 on success, or a negative errno on failure */
static int btusb_qcom_deactivate_msubsys_bt(struct hci_dev *hdev)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	int ret;

	ret = usb_autopm_get_interface(xport_data->intf);
	if (ret) {
		bt_dev_err(hdev, "get autopm for deactivate BT failed: %pe", ERR_PTR(ret));
		return ret;
	}

	ret = qdfu_activate_remote_btss(hdev, false, 100 * 1000);

	usb_autopm_put_interface(xport_data->intf);

	if (!ret)
		bt_dev_info(hdev, "deactivate BT succeeded");

	return ret;
}

/* return 0 on success, or a negative errno on failure */
static int btusb_qcom_sw_reset(struct hci_dev *hdev)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	int ret = -EOPNOTSUPP;

	if (!(qbt_data->flags & QBT_FLAG_SW_RESET))
		return ret;

	ret = usb_autopm_get_interface(xport_data->intf);
	if (ret) {
		bt_dev_err(hdev, "get autopm for SW reset failed: %pe", ERR_PTR(ret));
		return ret;
	}

	ret = qdfu_sw_reset(hdev);

	usb_autopm_put_interface(xport_data->intf);

	return ret;
}

#define QBT_RESET_TYPE_SYNC	"sync"
#define QBT_RESET_TYPE_ASYNC	"async"
#define QBT_RESET_TYPE_DIRECT	"direct"

/*
 * btusb_do_reset_work - reset the BTC
 * @hdev: the HCI device
 * @type: QBT_RESET_TYPE_* above, for logging
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_do_reset_work(struct hci_dev *hdev, void *type)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	struct gpio_desc *reset_gpio = xport_data->reset_gpio;
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	int ret;

	bt_dev_dbg(hdev, "%s reset: misc_flags(0x%lx)", (char *)type,
		   READ_ONCE(qbt_data->misc_flags));
	bt_dev_dbg(hdev, "md_state: %s, md_submit_err: %d",
		   hci_devcd_state_name(READ_ONCE(qbt_data->md_state)),
		   READ_ONCE(qbt_data->md_submit_err));

	if (test_and_set_bit(QBT_MISC_RESET_ACTIVE, &qbt_data->misc_flags)) {
		bt_dev_info(hdev, "reset already in progress");
		return 0;
	}

	if (xport_data->prepare_reset)
		xport_data->prepare_reset(hdev);

	if (reset_gpio) {
		bt_dev_info(hdev, "hardware reset");
		gpiod_set_value_cansleep(reset_gpio, 0);
		fsleep(200 * 1000);
		gpiod_set_value_cansleep(reset_gpio, 1);
		return 0;
	}

	ret = btusb_qcom_sw_reset(hdev);
	if (!ret)
		return 0;

	ret = usb_autopm_get_interface(xport_data->intf);
	if (ret) {
		bt_dev_err(hdev, "reset: autopm get failed: %pe", ERR_PTR(ret));
		clear_bit(QBT_MISC_RESET_ACTIVE, &qbt_data->misc_flags);
		return ret;
	}
	usb_autopm_put_interface_no_suspend(xport_data->intf);

	bt_dev_info(hdev, "usb reset");
	/* Clear it here since usb reset isn't guaranteed to succeed. */
	clear_bit(QBT_MISC_RESET_ACTIVE, &qbt_data->misc_flags);
	usb_queue_reset_device(xport_data->intf);

	return 0;
}

static void btusb_qcom_reset_sync(struct hci_dev *hdev)
{
	int res;

	res = hci_cmd_sync_queue_once(hdev, btusb_do_reset_work, QBT_RESET_TYPE_SYNC, NULL);
	if (res)
		bt_dev_dbg(hdev, "queue reset work failed: %pe", ERR_PTR(res));
	if (!res || res == -EEXIST || res == -ENODEV)
		return;

	btusb_do_reset_work(hdev, QBT_RESET_TYPE_DIRECT);
}

static void btqcom_reset_async(struct hci_dev *hdev, unsigned int delay_ms)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);

	set_bit(QBT_WORK_RESET_HDEV, &qbt_data->work_flags);
	schedule_delayed_work(&qbt_data->dwork, msecs_to_jiffies(delay_ms));
}

/*
 * btusb_do_cmd_timeout_work - HCI cmd sync work function to handle command timeout
 * @hdev: the HCI device
 * @data: unused
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_do_cmd_timeout_work(struct hci_dev *hdev,
				     void *data __maybe_unused)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	unsigned int wait_ms = 0;
	int ret = 0;

	if (!(qbt_data->flags & QBT_FLAG_MEMDUMP) ||
	    !(qbt_data->flags & QBT_FLAG_CMD_TIMEOUT_MEMDUMP))
		goto out_reset;

	switch (qbt_data->category) {
	case QBTC_CAT_LEGACY:
		return -EOPNOTSUPP;
	case QBTC_CAT_UNIFIED:
		wait_ms = 800;
		ret = qbt_error_fatal_cmd(hdev);
		break;
	case QBTC_CAT_MSUBSYS:
		wait_ms = 1200;
		ret = qperi_initiate_bt_crash(hdev);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		goto out_reset;

	/*
	 * Command timeout is solved by self-recovery after memdump.
	 * Testing CMD_TIMEOUT in this wait is only to abandon it
	 * immediately if required, not a sign the timeout is resolved.
	 */
	ret = wait_var_event_timeout(&qbt_data->misc_flags,
				     test_bit(QBT_MISC_MEMDUMP_INCOMING,
					      &qbt_data->misc_flags) ||
				     !test_bit(QBT_MISC_CMD_TIMEOUT,
					       &qbt_data->misc_flags),
				     msecs_to_jiffies(wait_ms));
	if (!test_bit(QBT_MISC_CMD_TIMEOUT, &qbt_data->misc_flags)) {
		bt_dev_dbg(hdev, "Flag CMD_TIMEOUT cleared");
		return 0;
	}
	if (ret) {
		bt_dev_dbg(hdev, "trigger memdump on command timeout succeeded");
		return 0;
	}
	bt_dev_err(hdev, "trigger memdump on command timeout failed");

out_reset:
	btusb_do_reset_work(hdev, QBT_RESET_TYPE_SYNC);

	return 0;
}

/* Implements hdev->reset(). */
static void btusb_qcom_reset(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	bool has_cmd_timeout;
	int res;

	has_cmd_timeout = current_work() == &hdev->cmd_timer.work;
	bt_dev_info(hdev, "reset triggered by %s",
		    has_cmd_timeout ? "command timeout" : "user");

	if (test_bit(QBT_MISC_MEMDUMP_PERI, &qbt_data->misc_flags) ||
	    test_bit(QBT_MISC_MEMDUMP_BT, &qbt_data->misc_flags) ||
	    test_bit(QBT_MISC_MEMDUMP_TMEL, &qbt_data->misc_flags)) {
		bt_dev_info(hdev, "reset will happen after memdump");
		return;
	}

	if (!has_cmd_timeout) {
		btusb_qcom_reset_sync(hdev);
		return;
	}

	if (test_and_set_bit(QBT_MISC_CMD_TIMEOUT, &qbt_data->misc_flags)) {
		bt_dev_info(hdev, "handling command timeout is in progress");
		return;
	}

	res = hci_cmd_sync_queue_once(hdev, btusb_do_cmd_timeout_work, NULL, NULL);
	if (res)
		bt_dev_dbg(hdev, "queue command timeout work failed: %pe", ERR_PTR(res));
	if (!res || res == -EEXIST || res == -ENODEV)
		return;

	btqcom_reset_async(hdev, 0);
}

/* Implements hdev->hw_error(). */
static void btqcom_hw_error(struct hci_dev *hdev, u8 code)
{
	const char *subsys_name = qhci_subsys_name(QHCI_SUBSYS_PERI);
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	int res;

	if (!test_bit(QBT_MISC_HWERR_PERI, &qbt_data->misc_flags)) {
		set_bit(QBT_MISC_HWERR_BT, &qbt_data->misc_flags);
		subsys_name = qhci_subsys_name(QHCI_SUBSYS_INVALID);
	}

	bt_dev_info(hdev, "%s hardware error (0x%02x)", subsys_name, code);
	bt_dev_dbg(hdev, "md_pending_flags: 0x%lx", qbt_data->md_pending_flags);
	bt_dev_dbg(hdev, "md_submit_err: %d", qbt_data->md_submit_err);

	if (qbt_data->md_pending_flags && !qbt_data->md_submit_err) {
		res = hci_devcd_complete(hdev);
		if (res)
			bt_dev_err(hdev, "memdump complete on %s hardware error failed: %pe",
				   subsys_name, ERR_PTR(res));
		else
			bt_dev_info(hdev, "memdump completed on %s hardware error: %u bytes",
				    subsys_name, qbt_data->md_submit_size);
	}

	qbt_data->md_pending_flags = 0;

	if (test_and_clear_bit(QBT_MISC_MEMDUMP_INCOMING, &qbt_data->misc_flags))
		bt_dev_dbg(hdev, "clear MEMDUMP_INCOMING on %s hardware error", subsys_name);
}

static int btusb_qcom_shutdown_unified(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	bool had_hwerr, had_memdump;
	unsigned int delay_ms = 10;
	int ret;

	had_memdump = test_bit(QBT_MISC_MEMDUMP_BT, &qbt_data->misc_flags);
	had_hwerr = test_bit(QBT_MISC_HWERR_BT, &qbt_data->misc_flags);

	bt_dev_dbg(hdev, "shutdown: had_memdump %d, had_hwerr %d",
		   had_memdump, had_hwerr);

	if (!had_hwerr && !had_memdump) {
		ret = __hci_reset_sync(hdev);
		if (ret)
			bt_dev_err(hdev, "shutdown: HCI reset failed: %pe",
				   ERR_PTR(ret));
		else
			bt_dev_info(hdev, "shutdown: HCI reset succeeded");
		goto out;
	}

	if (!had_memdump) {
		ret = __hci_cmd_sync_status(hdev, HCI_OP_RESET, 0, NULL,
					    HCI_CMD_TIMEOUT);
		if (!ret) {
			clear_bit(QBT_MISC_HWERR_BT, &qbt_data->misc_flags);
			bt_dev_info(hdev, "shutdown: recovered from hardware error via HCI reset");
			goto out;
		}

		bt_dev_warn(hdev, "shutdown: HCI reset failed to recover from hardware error: %pe",
			    ERR_PTR(ret));
	}

	ret = -EIO;
	if (had_memdump && !btqcom_has_btc_reset(hdev)) {
		bt_dev_info(hdev, "shutdown: BTC will self-recover via re-enumeration");
		return ret;
	}

	bt_dev_info(hdev, "shutdown: reset BTC after %u msec", delay_ms);
	btqcom_reset_async(hdev, delay_ms);

	return ret;
out:
	/*
	 * The flag is usually already unset here in a normal shutdown;
	 * clear it just in case. No wake_up_var() is needed since the
	 * waiter is never waiting at this time.
	 */
	if (test_and_clear_bit(QBT_MISC_CMD_TIMEOUT, &qbt_data->misc_flags))
		bt_dev_dbg(hdev, "clear CMD_TIMEOUT on shutdown");

	return ret;
}

static int btusb_qcom_shutdown_msubsys(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	bool had_peri_hwerr, had_peri_memdump;
	bool had_bt_hwerr, had_bt_memdump;
	bool had_tmel_memdump;
	unsigned int delay_ms;
	int ret;

	delay_ms = 10;
	had_peri_memdump = test_bit(QBT_MISC_MEMDUMP_PERI, &qbt_data->misc_flags);
	had_peri_hwerr = test_bit(QBT_MISC_HWERR_PERI, &qbt_data->misc_flags);
	had_bt_memdump = test_bit(QBT_MISC_MEMDUMP_BT, &qbt_data->misc_flags);
	had_bt_hwerr = test_bit(QBT_MISC_HWERR_BT, &qbt_data->misc_flags);
	had_tmel_memdump = test_bit(QBT_MISC_MEMDUMP_TMEL, &qbt_data->misc_flags);

	bt_dev_dbg(hdev, "shutdown: had_peri_memdump %d, had_peri_hwerr %d",
		   had_peri_memdump, had_peri_hwerr);
	bt_dev_dbg(hdev, "shutdown: had_bt_memdump %d, had_bt_hwerr %d",
		   had_bt_memdump, had_bt_hwerr);
	bt_dev_dbg(hdev, "shutdown: had_tmel_memdump %d", had_tmel_memdump);

	ret = -EIO;
	if (had_peri_memdump || had_peri_hwerr || had_tmel_memdump)
		goto out_reset;

	ret = btusb_qcom_deactivate_msubsys_bt(hdev);
	if (ret && (had_bt_memdump || had_bt_hwerr))
		goto out_reset;

	clear_bit(QBT_MISC_HWERR_BT, &qbt_data->misc_flags);
	clear_bit(QBT_MISC_MEMDUMP_BT, &qbt_data->misc_flags);
	/* Same as in btusb_qcom_shutdown_unified(). */
	if (test_and_clear_bit(QBT_MISC_CMD_TIMEOUT, &qbt_data->misc_flags))
		bt_dev_dbg(hdev, "clear CMD_TIMEOUT on shutdown");

	return ret;

out_reset:
	bt_dev_info(hdev, "shutdown: reset BTC after %u msec", delay_ms);
	btqcom_reset_async(hdev, delay_ms);
	return ret;
}

/* Implements hdev->shutdown(). */
static int btusb_qcom_shutdown(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	int ret;

	switch (qbt_data->category) {
	case QBTC_CAT_LEGACY:
		ret = -EOPNOTSUPP;
		break;
	case QBTC_CAT_UNIFIED:
		ret = btusb_qcom_shutdown_unified(hdev);
		break;
	case QBTC_CAT_MSUBSYS:
		ret = btusb_qcom_shutdown_msubsys(hdev);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * ============================================================================
 * QDFU firmware downloading
 * ============================================================================
 */

#define QBT_FW_PATH_MAX  96

enum qbt_fw_type {
	QBT_FW_TYPE_PATCH,
	QBT_FW_TYPE_NVM,
	QBT_FW_TYPE_MAX,
};

#define QBT_PATCH_TYPE_TLV		0x01
#define QBT_NVM_TYPE_TLV		0x02
struct qbt_tlv_file_hdr {
	u8	type;
	u8	length[3];
} __packed;

struct qbt_patch_tlv_hdr {
	struct  qbt_tlv_file_hdr tlv_hdr;
	__le32	total_len;
	__le32	patch_data_len;
	u8	sign_ver;
	u8	sign_algo;
	u8	download_cfg;
	u8	image_type;
	__le16	product_id;
	__le16	rom_ver;
	__le16	patch_ver;
	u8	reserved1[2];
	__le32	anti_rollback_ver;
	__le32	serial_low;
	__le16	serial_high;
	u8	debug_option;
	u8	reserved2;
	__le32	entry_addr;
} __packed;

struct qbt_nvm_tlv_hdr {
	struct  qbt_tlv_file_hdr tlv_hdr;
} __packed;

/*
 * struct qdfu_fw_cfg - QDFU firmware download configuration
 * @desc: short description for logging (e.g. "PERI patch", "PERI NVM")
 * @format: QBT_PATCH_TYPE_TLV or QBT_NVM_TYPE_TLV
 * @hdr_size: TLV header size for @format
 * @request: QDFU_BT_CMD_VSC_REQ_DOWNLOAD_(LOCAL|REMOTE)
 * @loaded_flag: QDFU_BT_STATE_* flag that marks fw downloaded or not
 * @settle_us: default time to wait after download for the BTC to settle
 * @get_path: build firmware file path, return number of paths or error
 * @check: check firmware, return 0 on success or a negative errno
 * @download: download firmware, return 0 on success or a negative errno
 * @get_settle_us: get the time to wait for BTC to settle
 * @post_download: run after download, return 0 on success or a negative errno
 */
struct qdfu_fw_cfg {
	const char	*desc;
	u8		format;
	u8		hdr_size;
	u8		request;
	u8		loaded_flag;
	u32		settle_us;

	int (*get_path)(struct hci_dev *hdev, const struct qdfu_fw_cfg *fw_cfg,
			const struct qbtc_info *info, const struct qdfu_bt_id *ctrl_id,
			char path[], size_t size);

	int (*check)(struct hci_dev *hdev, const struct qdfu_fw_cfg *fw_cfg,
		     const struct qbtc_info *info, const struct qdfu_bt_id *ctrl_id,
		     const struct firmware *fw);

	int (*download)(struct hci_dev *hdev, const struct qdfu_fw_cfg *fw_cfg,
			const struct qbtc_info *info, const struct qdfu_bt_id *ctrl_id,
			const void *data, size_t size);

	u32 (*get_settle_us)(struct hci_dev *hdev, const struct qdfu_fw_cfg *fw_cfg,
			     const struct qbtc_info *info, const struct qdfu_bt_id *ctrl_id);

	int (*post_download)(struct hci_dev *hdev, const struct qdfu_fw_cfg *fw_cfg,
			     const struct qbtc_info *info, const struct qdfu_bt_id *ctrl_id);
};

static int get_qbtc_subsys(const struct qdfu_fw_cfg *fw_cfg);

static void qhci_to_qdfu_id(struct qdfu_bt_id *dfu_id,
			    const struct qhci_btc_ver *hci_ver, u16 board_id)
{
	dfu_id->rom_version   = (u32)hci_ver->product_id << 16 | hci_ver->rom_ver;
	dfu_id->patch_version = hci_ver->patch_ver;
	dfu_id->soc_id        = hci_ver->soc_ver;
	dfu_id->board_id      = board_id;
}

static void qdfu_to_qhci_id(struct qhci_btc_ver *hci_ver, u16 *board_id,
			    const struct qdfu_bt_id *dfu_id)
{
	hci_ver->product_id = upper_16_bits(dfu_id->rom_version);
	hci_ver->rom_ver    = lower_16_bits(dfu_id->rom_version);
	hci_ver->patch_ver  = (u16)dfu_id->patch_version;
	hci_ver->soc_ver    = dfu_id->soc_id;
	*board_id           = dfu_id->board_id;
}

static int get_qdfu_id_via_hci(struct hci_dev *hdev,
			       struct qdfu_bt_id *dfu_id)
{
	struct qhci_btc_ver hci_ver;
	u16 board_id;
	int ret;

	ret = qbt_edl_patch_getver(hdev, &hci_ver);
	if (ret)
		return ret;

	ret = qbt_edl_get_board_id(hdev, &board_id);
	if (ret)
		return ret;

	qhci_to_qdfu_id(dfu_id, &hci_ver, board_id);

	return 0;
}

static const char *get_fw_directory(struct hci_dev *hdev, const struct qbtc_info *info,
				    const struct qdfu_bt_id *ctrl_id)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	const struct qbtc_bid_fwdir *custom_fwdir;

	if (qbt_data->fw_dir)
		return qbt_data->fw_dir;

	if (!info->custom_fw_table)
		goto info_fw_dir;

	for (custom_fwdir = info->custom_fw_table; custom_fwdir->board_id; custom_fwdir++) {
		if (custom_fwdir->board_id == ctrl_id->board_id)
			return custom_fwdir->fw_dir;
	}

info_fw_dir:
	if (info->fw_dir)
		return info->fw_dir;

	return "qca";
}

/*
 * btusb_get_patch_path - build the patch firmware file path
 * @hdev: the HCI device to build the path for
 * @fw_cfg: the firmware download config
 * @info: BTC info for this hdev
 * @ctrl_id: BTC QDFU ID
 * @path: output buffer for the built path
 * @size: size of @path
 *
 * Used as the qdfu_fw_cfg ->get_path() callback.
 *
 * Return: 1 path written into @path, or a negative errno on failure.
 */
static int btusb_get_patch_path(struct hci_dev *hdev,
				const struct qdfu_fw_cfg *fw_cfg,
				const struct qbtc_info *info,
				const struct qdfu_bt_id *ctrl_id,
				char path[], size_t size)
{
	const char *fw_dir = get_fw_directory(hdev, info, ctrl_id);
	int qbtc_subsys;
	int len;

	qbtc_subsys = get_qbtc_subsys(fw_cfg);

	if (qbtc_subsys == QBTC_SUBSYS_INVALID) {
		/* BT function unit. */
		len = snprintf(path, size, "%s/rampatch_usb_%08x.bin",
			       fw_dir, ctrl_id->rom_version);
		goto out;
	}

	switch (qbtc_subsys) {
	case QBTC_SUBSYS_PERI:
		len = snprintf(path, size, "%s/peripatch_usb_%08x.bin",
			       fw_dir, ctrl_id->rom_version);
		break;
	case QBTC_SUBSYS_TMEL:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

out:
	if (len >= size)
		return -ENAMETOOLONG;

	return 1;
}

/*
 * btusb_get_nvm_path - build the NVM firmware file path(s)
 * @hdev: the HCI device to build the path for
 * @fw_cfg: the firmware download config
 * @info: BTC info for this hdev
 * @ctrl_id: BTC QDFU ID
 * @path: output buffer for the built path(s)
 * @size: size of @path
 *
 * @path may hold two NUL-terminated paths back-to-back: a board-ID path
 * followed by a fallback path, when NVM fallback is enabled.
 *
 * Used as the qdfu_fw_cfg ->get_path() callback.
 *
 * Return: number of paths (1 or 2) written into @path, or a negative
 *	errno on failure.
 */
static int btusb_get_nvm_path(struct hci_dev *hdev,
			      const struct qdfu_fw_cfg *fw_cfg,
			      const struct qbtc_info *info,
			      const struct qdfu_bt_id *ctrl_id,
			      char path[], size_t size)
{
	const char *fw_dir = get_fw_directory(hdev, info, ctrl_id);
	const char *prefix = NULL, *foundry_str = NULL;
	bool has_bid = false, need_fb = false;
	char fw_fb[QBT_FW_PATH_MAX] = { 0 };
	int qbtc_subsys;
	int len = 0;

	qbtc_subsys = get_qbtc_subsys(fw_cfg);
	if (qbtc_subsys == QBTC_SUBSYS_INVALID) {
		/* BT function unit. */
		prefix = "nvm_usb";
	} else {
		switch (qbtc_subsys) {
		case QBTC_SUBSYS_PERI:
			prefix = "perinvm_usb";
			break;
		case QBTC_SUBSYS_TMEL:
			return -EOPNOTSUPP;
		default:
			return -EINVAL;
		}
	}

	if (info->flags & QBT_FLAG_FOUNDRY_NVM) {
		u8 foundry = FIELD_GET(GENMASK(15, 12), ctrl_id->soc_id);

		switch (foundry) {
		/* GlobalFoundries */
		case 0x01:
			foundry_str = "_gf";
			break;
		default:
			break;
		}
	}

	if ((info->flags & QBT_FLAG_BID_NVM) && ctrl_id->board_id) {
		has_bid = true;
		if (info->flags & QBT_FLAG_NVM_FALLBACK)
			need_fb = true;
	}

	len = snprintf(path, size, "%s/%s_%08x", fw_dir, prefix, ctrl_id->rom_version);
	if (len >= size)
		return -ENAMETOOLONG;

	if (foundry_str) {
		len += snprintf(path + len, size - len, "%s", foundry_str);
		if (len >= size)
			return -ENAMETOOLONG;
	}

	if (!has_bid) {
		len += snprintf(path + len, size - len, ".bin");
		if (len >= size)
			return -ENAMETOOLONG;
		return 1;
	}

	if (need_fb) {
		int fb_len = snprintf(fw_fb, sizeof(fw_fb), "%s.bin", path);

		if (fb_len >= sizeof(fw_fb))
			return -ENAMETOOLONG;

		len += snprintf(path + len, size - len, "_%04x.bin",
				ctrl_id->board_id);
		if (len >= size)
			return -ENAMETOOLONG;

		/* path holds two NUL-terminated strings back-to-back:
		 * [board-id path '\0'][fallback path '\0']
		 * Verify the buffer fits both before writing.
		 */
		if (len + fb_len + 2 > size)
			return -ENAMETOOLONG;

		len += 1;
		snprintf(path + len, size - len, "%s", fw_fb);
		return 2;
	}

	len += snprintf(path + len, size - len, "_%04x.bin", ctrl_id->board_id);
	if (len >= size)
		return -ENAMETOOLONG;

	return 1;
}

/* check the TLV file header; return 0 on success, or a negative errno */
static int qbt_check_tlv_file(struct hci_dev *hdev, const struct firmware *fw, u8 format)
{
	const struct qbt_tlv_file_hdr *tlv_hdr = (const struct qbt_tlv_file_hdr *)fw->data;
	size_t file_len;

	if (fw->size < sizeof(*tlv_hdr))
		return -ENODATA;

	if (tlv_hdr->type != format)
		return -EINVAL;

	file_len = get_unaligned_le24(tlv_hdr->length) + sizeof(*tlv_hdr);
	if (file_len != fw->size)
		return -EBADF;

	return 0;
}

/*
 * btusb_check_patch_tlv - validate a patch TLV file against the BTC
 *	before download
 * @hdev: the HCI device to validate the file for
 * @fw_cfg: the firmware download config (unused)
 * @info: BTC info for this hdev (unused)
 * @ctrl_id: BTC QDFU ID
 * @fw: the firmware to validate
 *
 * Used as the qdfu_fw_cfg ->check() callback.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_check_patch_tlv(struct hci_dev *hdev,
				 const struct qdfu_fw_cfg *fw_cfg,
				 const struct qbtc_info *info __maybe_unused,
				 const struct qdfu_bt_id *ctrl_id,
				 const struct firmware *fw)
{
	const struct qbt_patch_tlv_hdr *patch_hdr = (const void *)fw->data;
	u32 fw_rom_version, fw_patch_version;
	int ret;

	ret = qbt_check_tlv_file(hdev, fw, QBT_PATCH_TYPE_TLV);
	if (ret) {
		bt_dev_err(hdev, "Check %s TLV file header failed: %pe",
			   fw_cfg->desc, ERR_PTR(ret));
		return ret;
	}

	if (fw->size < sizeof(*patch_hdr)) {
		bt_dev_err(hdev, "%s header truncated (%zu bytes, header needs %zu)",
			   fw_cfg->desc, fw->size, sizeof(*patch_hdr));
		return -ENODATA;
	}

	if (likely(upper_16_bits(ctrl_id->rom_version))) {
		fw_rom_version = (le16_to_cpu(patch_hdr->product_id) << 16) |
				 le16_to_cpu(patch_hdr->rom_ver);
	} else {
		bt_dev_warn(hdev, "check %s: invalid rom_version 0x%08x",
			    fw_cfg->desc, ctrl_id->rom_version);
		fw_rom_version = le16_to_cpu(patch_hdr->rom_ver);
	}

	fw_patch_version = le16_to_cpu(patch_hdr->patch_ver);

	if (fw_rom_version != ctrl_id->rom_version) {
		bt_dev_err(hdev,
			   "%s ROM version 0x%08x does not match controller 0x%08x",
			   fw_cfg->desc, fw_rom_version, ctrl_id->rom_version);
		return -EINVAL;
	}

	if (fw_patch_version < ctrl_id->patch_version) {
		bt_dev_err(hdev,
			   "%s version 0x%x older than controller 0x%x, anti-rollback",
			   fw_cfg->desc, fw_patch_version, ctrl_id->patch_version);
		return -EPERM;
	}

	return 0;
}

/*
 * validate a NVM TLV file against the BTC
 * Used as the qdfu_fw_cfg ->check() callback.
 */
static int btusb_check_nvm_tlv(struct hci_dev *hdev,
			       const struct qdfu_fw_cfg *fw_cfg,
			       const struct qbtc_info *info __maybe_unused,
			       const struct qdfu_bt_id *ctrl_id __maybe_unused,
			       const struct firmware *fw)
{
	int ret;

	ret = qbt_check_tlv_file(hdev, fw, QBT_NVM_TYPE_TLV);
	if (ret) {
		bt_dev_err(hdev, "Check %s TLV file header failed: %pe",
			   fw_cfg->desc, ERR_PTR(ret));
		return ret;
	}

	return 0;
}

/*
 * btusb_download_via_qdfu - download a firmware image via QDFU to BTC
 * @hdev: the HCI device to send the firmware to
 * @fw_cfg: the firmware download config
 * @info: BTC info for this hdev (unused)
 * @ctrl_id: BTC QDFU ID (unused)
 * @fw_data: the firmware image to send, header included
 * @fw_size: size of @fw_data
 *
 * Used as the qdfu_fw_cfg ->download() callback.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_download_via_qdfu(struct hci_dev *hdev,
				   const struct qdfu_fw_cfg *fw_cfg,
				   const struct qbtc_info *info __maybe_unused,
				   const struct qdfu_bt_id *ctrl_id __maybe_unused,
				   const void *fw_data, size_t fw_size)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	unsigned int pipe = usb_sndbulkpipe(xport_data->udev, 0x02);
	const unsigned int xfer_timeout_ms = 3000;
	const size_t xfer_size = SZ_4K;
	const u8 *base = fw_data;
	const u8 *ptr = fw_data;
	size_t seg_size, size;
	int snd_len, ret;
	u8 *buf;

	ret = qdfu_vsc_req_download(hdev, fw_cfg->request, ptr, fw_cfg->hdr_size);
	if (ret)
		return ret;

	ptr += fw_cfg->hdr_size;
	size = fw_size - fw_cfg->hdr_size;

	/* ep2 needs time to switch from ACL to DFU function mode. */
	fsleep(20 * 1000);

	buf = kmalloc(xfer_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (size) {
		seg_size = min_t(size_t, size, xfer_size);
		memcpy(buf, ptr, seg_size);

		snd_len = 0;
		ret = usb_bulk_msg(xport_data->udev, pipe, buf, seg_size, &snd_len,
				   xfer_timeout_ms);
		if (ret < 0) {
			bt_dev_err(hdev,
				   "QDFU bulk send failed at offset %zu: %pe",
				   ptr - base, ERR_PTR(ret));
			break;
		}

		if (seg_size != snd_len) {
			bt_dev_err(hdev,
				   "QDFU bulk short write (%d of %zu bytes) at offset %zu",
				   snd_len, seg_size, ptr - base);
			ret = -EIO;
			break;
		}

		ptr += seg_size;
		size -= seg_size;
	}

	kfree(buf);
	return ret;
}

/*
 * Used as the qdfu_fw_cfg ->get_settle_us() callback.
 * Kept here for future per-BTC settle time use.
 */
static u32 btusb_get_settle_us(struct hci_dev *hdev __maybe_unused,
			       const struct qdfu_fw_cfg *fw_cfg,
			       const struct qbtc_info *info __maybe_unused,
			       const struct qdfu_bt_id *ctrl_id __maybe_unused)
{
	return fw_cfg->settle_us;
}

/* Used as the qdfu_fw_cfg ->post_download() callback. */
static int btusb_post_download_unified_bt(struct hci_dev *hdev,
					  const struct qdfu_fw_cfg *fw_cfg __maybe_unused,
					  const struct qbtc_info *info __maybe_unused,
					  const struct qdfu_bt_id *ctrl_id __maybe_unused)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	char *build_info = NULL;
	int ret;

	ret = qbt_edl_get_build_info(hdev, &build_info);
	if (!ret) {
		strscpy(qbt_data->build_info, build_info, sizeof(qbt_data->build_info));
		kfree(build_info);
	}

	ret = __hci_reset_sync(hdev);
	if (ret)
		bt_dev_err(hdev, "HCI reset after BT NVM download failed: %pe", ERR_PTR(ret));
	else
		bt_dev_info(hdev, "HCI reset after BT NVM download succeeded");

	return ret;
}

/* Used as the qdfu_fw_cfg ->post_download() callback. */
static int btusb_post_download_msubsys_peri(struct hci_dev *hdev,
					    const struct qdfu_fw_cfg *fw_cfg __maybe_unused,
					    const struct qbtc_info *info __maybe_unused,
					    const struct qdfu_bt_id *ctrl_id __maybe_unused)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct qbtc_subsys_data *subsys_data;
	char *build_info = NULL;
	int ret;

	subsys_data = &qbt_data->subsys[QBTC_SUBSYS_PERI];

	ret = qperi_get_subsys_build_info(hdev, QHCI_SUBSYS_PERI, &build_info);
	if (!ret) {
		strscpy(subsys_data->build_info, build_info, sizeof(subsys_data->build_info));
		kfree(build_info);
	}

	/* Resetting PERI HCI is only allowed after downloading PERI NVM. */
	if (qbt_data->flags & QBT_FLAG_RESET_PERI_HCI)
		ret = qperi_hci_reset(hdev);
	else
		ret = qdfu_reset_peri_hci(hdev);

	return ret;
}

static const struct qdfu_fw_cfg qdfu_fw_cfgs_bt[QBTC_CAT_MAX][QBT_FW_TYPE_MAX] = {
	[QBTC_CAT_UNIFIED][QBT_FW_TYPE_PATCH] = {
		.desc          = "patch",
		.format        = QBT_PATCH_TYPE_TLV,
		.hdr_size      = sizeof(struct qbt_patch_tlv_hdr),
		.request       = QDFU_BT_CMD_VSC_REQ_DOWNLOAD_LOCAL,
		.loaded_flag   = QDFU_BT_STATE_PATCHED_LOCAL,
		.settle_us     = 10 * 1000,
		.get_path      = btusb_get_patch_path,
		.check         = btusb_check_patch_tlv,
		.download      = btusb_download_via_qdfu,
		.get_settle_us = btusb_get_settle_us,
		.post_download = NULL,
	},
	[QBTC_CAT_UNIFIED][QBT_FW_TYPE_NVM] = {
		.desc          = "NVM",
		.format        = QBT_NVM_TYPE_TLV,
		.hdr_size      = sizeof(struct qbt_nvm_tlv_hdr),
		.request       = QDFU_BT_CMD_VSC_REQ_DOWNLOAD_LOCAL,
		.loaded_flag   = QDFU_BT_STATE_NVMED_LOCAL,
		.settle_us     = 40 * 1000,
		.get_path      = btusb_get_nvm_path,
		.check         = btusb_check_nvm_tlv,
		.download      = btusb_download_via_qdfu,
		.get_settle_us = btusb_get_settle_us,
		.post_download = btusb_post_download_unified_bt,
	},
	[QBTC_CAT_MSUBSYS][QBT_FW_TYPE_PATCH] = {
		.desc          = "BT patch",
		.format        = QBT_PATCH_TYPE_TLV,
		.hdr_size      = sizeof(struct qbt_patch_tlv_hdr),
		.request       = QDFU_BT_CMD_VSC_REQ_DOWNLOAD_REMOTE,
		.loaded_flag   = QDFU_BT_STATE_PATCHED_REMOTE,
		.settle_us     = 30 * 1000,
		.get_path      = btusb_get_patch_path,
		.check         = btusb_check_patch_tlv,
		.download      = btusb_download_via_qdfu,
		.get_settle_us = btusb_get_settle_us,
		.post_download = NULL,
	},
	[QBTC_CAT_MSUBSYS][QBT_FW_TYPE_NVM] = {
		.desc          = "BT NVM",
		.format        = QBT_NVM_TYPE_TLV,
		.hdr_size      = sizeof(struct qbt_nvm_tlv_hdr),
		.request       = QDFU_BT_CMD_VSC_REQ_DOWNLOAD_REMOTE,
		.loaded_flag   = QDFU_BT_STATE_NVMED_REMOTE,
		.settle_us     = 60 * 1000,
		.get_path      = btusb_get_nvm_path,
		.check         = btusb_check_nvm_tlv,
		.download      = btusb_download_via_qdfu,
		.get_settle_us = btusb_get_settle_us,
		.post_download = btusb_post_download_unified_bt,
	},
};

static const struct qdfu_fw_cfg qdfu_fw_cfgs_subsys[QBTC_SUBSYS_MAX][QBT_FW_TYPE_MAX] = {
	[QBTC_SUBSYS_PERI][QBT_FW_TYPE_PATCH] = {
		.desc          = "PERI patch",
		.format        = QBT_PATCH_TYPE_TLV,
		.hdr_size      = sizeof(struct qbt_patch_tlv_hdr),
		.request       = QDFU_BT_CMD_VSC_REQ_DOWNLOAD_LOCAL,
		.loaded_flag   = QDFU_BT_STATE_PATCHED_LOCAL,
		.settle_us     = 10 * 1000,
		.get_path      = btusb_get_patch_path,
		.check         = btusb_check_patch_tlv,
		.download      = btusb_download_via_qdfu,
		.get_settle_us = btusb_get_settle_us,
		.post_download = NULL,
	},
	[QBTC_SUBSYS_PERI][QBT_FW_TYPE_NVM] = {
		.desc          = "PERI NVM",
		.format        = QBT_NVM_TYPE_TLV,
		.hdr_size      = sizeof(struct qbt_nvm_tlv_hdr),
		.request       = QDFU_BT_CMD_VSC_REQ_DOWNLOAD_LOCAL,
		.loaded_flag   = QDFU_BT_STATE_NVMED_LOCAL,
		.settle_us     = 20 * 1000,
		.get_path      = btusb_get_nvm_path,
		.check         = btusb_check_nvm_tlv,
		.download      = btusb_download_via_qdfu,
		.get_settle_us = btusb_get_settle_us,
		.post_download = btusb_post_download_msubsys_peri,
	},
};

static int get_qbtc_subsys(const struct qdfu_fw_cfg *fw_cfg)
{
	int qbtc_subsys;
	int fw_type;

	for (qbtc_subsys = 0; qbtc_subsys < QBTC_SUBSYS_MAX; qbtc_subsys++) {
		for (fw_type = 0; fw_type < QBT_FW_TYPE_MAX; fw_type++) {
			if (&qdfu_fw_cfgs_subsys[qbtc_subsys][fw_type] == fw_cfg)
				return qbtc_subsys;
		}
	}

	return QBTC_SUBSYS_INVALID;
}

/*
 * btusb_download_fw - download a firmware image
 * @hdev: the HCI device to download the firmware to
 * @fw_cfg: the firmware download config
 * @info: BTC info for this hdev
 * @ctrl_id: BTC ID
 *
 * Return: 1 if downloaded, 0 if already downloaded, or a negative errno
 *	on failure.
 */
static int btusb_download_fw(struct hci_dev *hdev,
			     const struct qdfu_fw_cfg *fw_cfg,
			     const struct qbtc_info *info,
			     const struct qdfu_bt_id *ctrl_id)
{
	const struct firmware *fw = NULL;
	char paths[QBT_FW_PATH_MAX * 2];
	const char *loaded_name;
	u8 state, state_new;
	u32 settle_us;
	int n_paths;
	int ret;

	bt_dev_dbg(hdev, "download %s", fw_cfg->desc);
	ret = qdfu_get_target_state(hdev, &state);
	if (ret)
		goto out;

	if (state & fw_cfg->loaded_flag) {
		bt_dev_info(hdev, "%s already downloaded", fw_cfg->desc);
		goto out;
	}

	n_paths = fw_cfg->get_path(hdev, fw_cfg, info, ctrl_id, paths, sizeof(paths));
	if (n_paths < 0) {
		bt_dev_err(hdev, "Failed to get %s path: %pe",
			   fw_cfg->desc, ERR_PTR(n_paths));
		ret = n_paths;
		goto out;
	}
	bt_dev_dbg(hdev, "%s path: %s", fw_cfg->desc, paths);
	if (n_paths == 2)
		bt_dev_dbg(hdev, "%s fallback path: %s", fw_cfg->desc,
			   paths + strlen(paths) + 1);

	loaded_name = paths;
	ret = request_firmware(&fw, paths, &hdev->dev);
	if (ret == -ENOENT && n_paths == 2) {
		bt_dev_err(hdev, "Failed to request %s %s: %pe",
			   fw_cfg->desc, loaded_name, ERR_PTR(ret));
		loaded_name = paths + strlen(paths) + 1;
		ret = request_firmware(&fw, loaded_name, &hdev->dev);
	}
	if (ret) {
		bt_dev_err(hdev, "Failed to request %s %s: %pe",
			   fw_cfg->desc, loaded_name, ERR_PTR(ret));
		goto out;
	}
	bt_dev_dbg(hdev, "%s request succeeded: %s", fw_cfg->desc, loaded_name);

	ret = fw_cfg->check(hdev, fw_cfg, info, ctrl_id, fw);
	if (ret)
		goto out_free_fw;

	ret = fw_cfg->download(hdev, fw_cfg, info, ctrl_id, fw->data, fw->size);
	if (ret)
		goto out_free_fw;
	bt_dev_info(hdev, "%s download succeeded: %s", fw_cfg->desc, loaded_name);

	ret = qdfu_poll_state(hdev, &state_new, true, fw_cfg->loaded_flag);
	if (ret)
		goto out_free_fw;
	if (state_new != state)
		bt_dev_dbg(hdev, "%s state: 0x%02x -> 0x%02x",
			   fw_cfg->desc, state, state_new);

	if (fw_cfg->get_settle_us)
		settle_us = fw_cfg->get_settle_us(hdev, fw_cfg, info, ctrl_id);
	else
		settle_us = fw_cfg->settle_us;
	fsleep(settle_us);

	if (fw_cfg->post_download) {
		ret = fw_cfg->post_download(hdev, fw_cfg, info, ctrl_id);
		if (ret)
			bt_dev_err(hdev, "%s post-download failed: %pe",
				   fw_cfg->desc, ERR_PTR(ret));
		else
			bt_dev_dbg(hdev, "%s post-download succeeded", fw_cfg->desc);
	}

	if (!ret) {
		bt_dev_dbg(hdev, "download %s succeeded", fw_cfg->desc);
		ret = 1;
	}

out_free_fw:
	release_firmware(fw);
out:
	if (ret < 0)
		bt_dev_err(hdev, "download %s failed: %pe", fw_cfg->desc, ERR_PTR(ret));
	return ret;
}

/*
 * ============================================================================
 * btusb_qcom.h APIs
 * ============================================================================
 */

/* size of the hci priv area */
int btusb_qcom_hdev_priv_size(void)
{
	return sizeof(struct btqcom_data) + sizeof(struct btusb_qcom);
}

/* get @hdev's transport-specific data */
struct btusb_qcom *btusb_qcom_xport_data(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);

	return (struct btusb_qcom *)(qbt_data + 1);
}

/* work function for qbt_data->dwork */
static void btqcom_work(struct work_struct *work)
{
	struct btqcom_data *qbt_data;
	struct hci_dev *hdev;

	qbt_data = container_of(to_delayed_work(work), struct btqcom_data, dwork);
	hdev = qbt_data->hdev;

	if (test_and_clear_bit(QBT_WORK_RESET_HDEV, &qbt_data->work_flags)) {
		hci_dev_hold(hdev);
		hci_req_sync_lock(hdev);

		btusb_do_reset_work(hdev, QBT_RESET_TYPE_ASYNC);

		hci_req_sync_unlock(hdev);
		hci_dev_put(hdev);
	}
}

static void devm_btusb_qcom_deinit(void *data)
{
	struct btqcom_data *qbt_data = data;
	struct btusb_qcom *xport_data;

	if (!qbt_data->inited)
		return;

	xport_data = qbt_data->xport_data;
	mutex_destroy(&qbt_data->req_mutex);
	mutex_destroy(&xport_data->tx_mutex);

	qbt_data->inited = false;
	bt_dev_dbg(qbt_data->hdev, "btusb qcom deinited");
}

static void __maybe_unused btusb_qcom_deinit(struct btqcom_data *qbt_data)
{
	devm_release_action(&qbt_data->hdev->dev, devm_btusb_qcom_deinit,
			    qbt_data);
}

/*
 * btusb_qcom_init - initialize hdev and per-device btqcom_data
 * @qbt_data: the btqcom_data to initialize
 *
 * Doesn't touch btqcom_data fields that live for the hdev's lifetime,
 * e.g. @fw_dir and @fw_logging.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_qcom_init(struct btqcom_data *qbt_data)
{
	struct btusb_qcom *xport_data = qbt_data->xport_data;
	struct hci_dev *hdev = qbt_data->hdev;
	int res;

	WRITE_ONCE(qbt_data->misc_flags, 0);
	WRITE_ONCE(qbt_data->work_flags, 0);

	qbt_data->md_state = HCI_DEVCOREDUMP_IDLE;
	qbt_data->md_submit_err = 0;
	qbt_data->md_submit_size = 0;
	qbt_data->md_pending_flags = 0;
	btqcom_reset_memdump(&qbt_data->md);

	WRITE_ONCE(qbt_data->req, NULL);
	WRITE_ONCE(qbt_data->req_state, QHCI_REQ_DONE);

	if (qbt_data->inited)
		return 0;

	hci_set_quirk(hdev, HCI_QUIRK_NON_PERSISTENT_SETUP);
	hci_set_quirk(hdev, HCI_QUIRK_SIMULTANEOUS_DISCOVERY);
	hci_set_quirk(hdev, HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED);

	if (qbt_data->flags & QBT_FLAG_AOSP_EXT)
		hci_set_aosp_capable(hdev);
	if (qbt_data->flags & QBT_FLAG_MSFT_EXT)
		hci_set_msft_opcode(hdev, 0xFD70);

	res = devm_add_action(&hdev->dev, devm_btusb_qcom_deinit, qbt_data);
	if (res) {
		bt_dev_err(hdev, "Add btusb qcom deinit action failed: %pe",
			   ERR_PTR(res));
		return res;
	}

	INIT_DELAYED_WORK(&qbt_data->dwork, btqcom_work);
	mutex_init(&xport_data->tx_mutex);
	mutex_init(&qbt_data->req_mutex);
	init_waitqueue_head(&qbt_data->req_wait_q);
	spin_lock_init(&qbt_data->req_spinlock);

	hdev->set_bdaddr        = btqcom_set_bdaddr;
	hdev->reset             = btusb_qcom_reset;
	hdev->shutdown          = btusb_qcom_shutdown;
	hdev->hw_error          = btqcom_hw_error;
	hdev->handle_ev_vendor  = btqcom_handle_ev_vendor;
	hdev->recv_vendor_pkt   = btqcom_recv_vendor_pkt;

	qbt_data->md_ready = false;
	if (qbt_data->flags & QBT_FLAG_MEMDUMP) {
		res = hci_devcd_register(hdev, btusb_qcom_trigger_memdump,
					 btusb_qcom_memdump_hdr,
					 btusb_qcom_notify_memdump);
		if (!res)
			qbt_data->md_ready = true;
		else if (res == -EOPNOTSUPP)
			bt_dev_warn(hdev, "CONFIG_DEV_COREDUMP not enabled");
		else
			bt_dev_err(hdev, "register devcoredump failed: %pe",
				   ERR_PTR(res));
	}

	/* pairs with smp_load_acquire() in btusb_qcom_disconnect() */
	smp_store_release(&qbt_data->inited, true);

	bt_dev_dbg(hdev, "btusb qcom inited");
	return 0;
}

/*
 * btusb_qcom_setup_unified - setup for a unified BTC
 * @hdev: the HCI device to set up
 * @info: BTC info for this hdev
 * @ctrl_id: BTC QDFU ID
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_qcom_setup_unified(struct hci_dev *hdev,
				    const struct qbtc_info *info,
				    const struct qdfu_bt_id *ctrl_id)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	enum qbtc_category btc_cat = info->category;
	const struct qdfu_fw_cfg *cfgs_bt;
	struct qdfu_bt_id bt_id;
	int ret;

	cfgs_bt = qdfu_fw_cfgs_bt[btc_cat];

	ret = btusb_download_fw(hdev, &cfgs_bt[QBT_FW_TYPE_PATCH], info, ctrl_id);
	if (ret < 0)
		return ret;

	ret = qdfu_get_target_version(hdev, &bt_id);
	if (ret)
		return ret;
	qdfu_to_qhci_id(&qbt_data->ver, &qbt_data->board_id, &bt_id);

	ret = btusb_download_fw(hdev, &cfgs_bt[QBT_FW_TYPE_NVM], info, &bt_id);
	if (ret)
		return ret < 0 ? ret : 0;

	ret = __hci_reset_sync(hdev);
	if (ret)
		bt_dev_err(hdev, "HCI reset failed: %pe", ERR_PTR(ret));
	else
		bt_dev_dbg(hdev, "HCI reset succeeded");

	return ret;
}

/*
 * btusb_qcom_setup_msubsys - setup for a multi-subsystem BTC
 * @hdev: the HCI device to set up
 * @info: BTC info for this hdev
 * @ctrl_id: BTC QDFU ID
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_qcom_setup_msubsys(struct hci_dev *hdev,
				    const struct qbtc_info *info,
				    const struct qdfu_bt_id *ctrl_id)
{
	const struct qdfu_fw_cfg *cfgs_peri = qdfu_fw_cfgs_subsys[QBTC_SUBSYS_PERI];
	const struct qdfu_fw_cfg *cfgs_bt = qdfu_fw_cfgs_bt[QBTC_CAT_MSUBSYS];
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct qbtc_subsys_data *subsys_peri;
	struct qdfu_bt_id peri_id, bt_id;
	int ret;

	subsys_peri = &qbt_data->subsys[QBTC_SUBSYS_PERI];

	ret = btusb_download_fw(hdev, &cfgs_peri[QBT_FW_TYPE_PATCH], info, ctrl_id);
	if (ret < 0)
		return ret;

	ret = qdfu_get_target_version(hdev, &peri_id);
	if (ret)
		return ret;
	qdfu_to_qhci_id(&subsys_peri->ver, &subsys_peri->board_id, &peri_id);

	ret = btusb_download_fw(hdev, &cfgs_peri[QBT_FW_TYPE_NVM], info, &peri_id);
	if (ret < 0)
		return ret;

	ret = qdfu_reset_msubsys_bt(hdev);
	if (ret)
		return ret;

	ret = get_qdfu_id_via_hci(hdev, &bt_id);
	if (ret)
		return ret;

	if (bt_id.rom_version != peri_id.rom_version ||
	    bt_id.soc_id != peri_id.soc_id ||
	    bt_id.board_id != peri_id.board_id) {
		bt_dev_info(hdev, "BT ROM Version: 0x%08x",
			    bt_id.rom_version);
		bt_dev_info(hdev, "BT Patch Version: 0x%08x",
			    bt_id.patch_version);
		bt_dev_info(hdev, "BT SoC Version: 0x%08x",
			    bt_id.soc_id);
		bt_dev_info(hdev, "BT Board ID: 0x%04x",
			    bt_id.board_id);
	}

	ret = btusb_download_fw(hdev, &cfgs_bt[QBT_FW_TYPE_PATCH], info, &bt_id);
	if (ret < 0)
		return ret;

	ret = get_qdfu_id_via_hci(hdev, &bt_id);
	if (ret)
		return ret;
	qdfu_to_qhci_id(&qbt_data->ver, &qbt_data->board_id, &bt_id);

	ret = btusb_download_fw(hdev, &cfgs_bt[QBT_FW_TYPE_NVM], info, &bt_id);
	if (ret < 0)
		return ret;

	return 0;
}

static int btqcom_post_setup(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	struct qbtc_subsys_data *subsys_data;
	char *build_info = NULL;
	char buf[512];
	int len = 0;
	int res;

	bt_dev_dbg(hdev, "flags: 0x%lx", qbt_data->flags);
	bt_dev_info(hdev, "memdump ready? %s", str_yes_no(qbt_data->md_ready));

	qbt_config_fw_logging(hdev, qbt_data->fw_logging);

	hci_set_hw_info(hdev, "%s", qbt_data->btc_name);
	if (qbt_data->category == QBTC_CAT_MSUBSYS) {
		subsys_data = &qbt_data->subsys[QBTC_SUBSYS_PERI];
		if (subsys_data->build_info[0] == '\0') {
			res = qperi_get_subsys_build_info(hdev, QHCI_SUBSYS_PERI,
							  &build_info);
			if (!res) {
				strscpy(subsys_data->build_info, build_info,
					sizeof(subsys_data->build_info));
				kfree(build_info);
			}
		}

		bt_dev_info(hdev, "%s build info: %s",
			    qbtc_subsys_name(QBTC_SUBSYS_PERI), subsys_data->build_info);
		len = scnprintf(buf, sizeof(buf), "%s: %s\n",
				qbtc_subsys_name(QBTC_SUBSYS_PERI), subsys_data->build_info);
	}

	if (qbt_data->build_info[0] == '\0') {
		build_info = NULL;
		res = qbt_edl_get_build_info(hdev, &build_info);
		if (!res) {
			strscpy(qbt_data->build_info, build_info,
				sizeof(qbt_data->build_info));
			kfree(build_info);
		}
	}

	bt_dev_info(hdev, "BT build info: %s", qbt_data->build_info);
	len += snprintf(buf + len, sizeof(buf) - len, "BT: %s", qbt_data->build_info);
	hci_set_fw_info(hdev, "%s", buf);

	return 0;
}

/*
 * btusb_qcom_setup - setup a BTC
 * @hdev: the HCI device
 *
 * Identifies the BTC, then dispatches setup based on its category.
 * Implements hdev->setup().
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int btusb_qcom_setup(struct hci_dev *hdev)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	bool is_first_setup = hci_dev_test_flag(hdev, HCI_SETUP);
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	const struct qbtc_id *id_entry = qbtc_id_table;
	struct qdfu_bt_id ctrl_id;
	int ret;

	qbt_data->hdev = hdev;
	xport_data->intf = to_usb_interface(hdev->dev.parent);
	xport_data->udev = interface_to_usbdev(xport_data->intf);
	xport_data->idVendor  = le16_to_cpu(xport_data->udev->descriptor.idVendor);
	xport_data->idProduct = le16_to_cpu(xport_data->udev->descriptor.idProduct);
	qbt_data->xport_data = xport_data;

	ret = usb_autopm_get_interface(xport_data->intf);
	if (ret) {
		bt_dev_err(hdev, "get autopm for QCOM BTUSB setup failed: %pe",
			   ERR_PTR(ret));
		return ret;
	}

	ret = qdfu_get_target_version(hdev, &ctrl_id);
	if (ret)
		goto out;

	while (id_entry->rom_version) {
		if (id_entry->rom_version == ctrl_id.rom_version)
			break;
		id_entry++;
	}

	if (!id_entry->rom_version) {
		ret = -ENODEV;
		bt_dev_warn(hdev, "Detected unsupported BT controller:");
	} else {
		bt_dev_info(hdev, "%s QCOM %s BT controller: %s",
			    is_first_setup ? "Detected" : "Setup",
			    qbtc_category_name(id_entry->btc_info->category),
			    id_entry->name);
	}

	bt_dev_info(hdev, "ROM Version  : 0x%08x", ctrl_id.rom_version);
	bt_dev_info(hdev, "Patch Version: 0x%08x", ctrl_id.patch_version);
	bt_dev_info(hdev, "SoC Version  : 0x%08x", ctrl_id.soc_id);
	bt_dev_info(hdev, "Board ID     : 0x%04x", ctrl_id.board_id);

	if (ret)
		goto out;

	if (!qbt_data->drv_name)
		qbt_data->drv_name = dev_driver_string(&xport_data->intf->dev);
	qbt_data->btc_name = id_entry->name;
	qbt_data->category = id_entry->btc_info->category;
	qbt_data->flags    = id_entry->btc_info->flags & QBT_FLAG_BTC_CFG_MASK;
	if (xport_data->reset_gpio)
		qbt_data->flags |= QBT_FLAG_HW_RESET;
	ret = btusb_qcom_init(qbt_data);
	if (ret)
		goto out;

	switch (qbt_data->category) {
	case QBTC_CAT_LEGACY:
		ret = -EOPNOTSUPP;
		break;
	case QBTC_CAT_UNIFIED:
		hci_set_quirk(hdev, HCI_QUIRK_BROKEN_ENHANCED_SETUP_SYNC_CONN);
		ret = btusb_qcom_setup_unified(hdev, id_entry->btc_info, &ctrl_id);
		break;
	case QBTC_CAT_MSUBSYS:
		ret = btusb_qcom_setup_msubsys(hdev, id_entry->btc_info, &ctrl_id);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret)
		ret = btqcom_post_setup(hdev);

out:
	usb_autopm_put_interface(xport_data->intf);
	if (!ret)
		bt_dev_info(hdev, "QCOM BTUSB setup succeeded (^_^)");
	else
		bt_dev_err(hdev, "QCOM BTUSB setup failed: %pe", ERR_PTR(ret));
	return ret;
}

/*
 * btusb_qcom_disconnect - clean up on USB disconnect
 * @hdev: the HCI device being disconnected
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int btusb_qcom_disconnect(struct hci_dev *hdev)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);

	/* pairs with smp_store_release() in btusb_qcom_init() */
	if (!smp_load_acquire(&qbt_data->inited))
		return 0;

	bt_dev_dbg(hdev, "disconnection: misc_flags(0x%lx)",
		   READ_ONCE(qbt_data->misc_flags));

	if (test_bit(QBT_MISC_MEMDUMP_INCOMING, &qbt_data->misc_flags) &&
	    !qbt_data->md_submit_err)
		bt_dev_warn(hdev, "memdump collection interrupted by disconnection");

	qperi_tx_sync_cancel_sync(hdev, -ENODEV);
	if (test_and_clear_bit(QBT_MISC_CMD_TIMEOUT, &qbt_data->misc_flags)) {
		wake_up_var(&qbt_data->misc_flags);
		bt_dev_dbg(hdev, "wake command-timeout waiter on disconnection");
	}
	disable_delayed_work_sync(&qbt_data->dwork);

	return 0;
}

/*
 * btusb_qcom_send_frame - send a frame for BT or PERI
 * @hdev: the HCI device
 * @skb: the frame to send
 *
 * Implements hdev->send().
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int btusb_qcom_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
	__context_unsafe(/* conditional locking */)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	bool is_vendor;
	bool need_lock;
	u8 pkt_type;
	int ret;

	if (qbt_data->category != QBTC_CAT_MSUBSYS)
		return xport_data->send_bt_frame(hdev, skb);

	pkt_type = hci_skb_pkt_type(skb);
	is_vendor = pkt_type == HCI_VENDOR_PKT;
	/* an empty vendor packet has no path to reach here */
	if (is_vendor)
		hci_skb_pkt_type(skb) = *(const u8 *)skb_pull_data(skb, 1);

	pkt_type = hci_skb_pkt_type(skb);
	need_lock = pkt_type == HCI_COMMAND_PKT ||
		    pkt_type == QPERI_COMMAND_PKT;

	if (need_lock)
		mutex_lock(&xport_data->tx_mutex);

	if (is_vendor)
		ret = xport_data->send_vendor_frame(hdev, skb);
	else
		ret = xport_data->send_bt_frame(hdev, skb);

	if (need_lock)
		mutex_unlock(&xport_data->tx_mutex);

	return ret;
}

/*
 * RX flow:
 *
 * USB intr/bulk endpoint (byte stream)
 *  -> btusb_qcom_recv_{intr,bulk}() (reassemble to frame)
 *      -> btqcom_recv_frame()
 *          -> btusb_upward_frame() (dispatch frame upwards)
 *              -> hci_recv_frame() (PERI frame as HCI_VENDOR_PKT)
 *              -> xport_data->recv_bt_frame() (BT, back to btusb_main.c)
 */

/*
 * btusb_upward_frame - dispatch a received frame upwards
 * @hdev: the HCI device the @skb comes from
 * @skb: the frame to dispatch upwards
 *
 * Return: 0 on success, or a negative errno on failure.
 */
static int btusb_upward_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_qcom *xport_data = btusb_qcom_xport_data(hdev);
	u8 pkt_type = hci_skb_pkt_type(skb);
	u16 handle;
	int ret;

	switch (pkt_type) {
	case QPERI_EVENT_PKT:
	case QPERI_ACLDATA_PKT:
		*(u8 *)skb_push(skb, 1) = pkt_type;
		hci_skb_pkt_type(skb) = HCI_VENDOR_PKT;
		break;
	case HCI_EVENT_PKT:
		break;
	case HCI_ACLDATA_PKT:
		handle = hci_acl_handle(skb);
		/* reroute vendor ACLs as HCI_VENDOR_PKT */
		if (handle == QBT_HANDLE_ENHANCED_LOGGING ||
		    handle == QBT_HANDLE_MEMDUMP) {
			*(u8 *)skb_push(skb, 1) = pkt_type;
			hci_skb_pkt_type(skb) = HCI_VENDOR_PKT;
		}
		break;
	default:
		dev_kfree_skb_irq(skb);
		ret = -EINVAL;
		goto out;
	}

	if (hci_skb_pkt_type(skb) == HCI_VENDOR_PKT)
		ret = hci_recv_frame(hdev, skb);
	else
		ret = xport_data->recv_bt_frame(hdev, skb);

out:
	if (ret)
		bt_dev_err(hdev, "upward frame with type (0x%02x) failed: %pe",
			   pkt_type, ERR_PTR(ret));

	return ret;
}

/* Every frame lands here first — the ideal spot for pre-processing. */
static int btqcom_recv_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	u8 pkt_type = hci_skb_pkt_type(skb);

	switch (pkt_type) {
	case HCI_EVENT_PKT:
	case QPERI_EVENT_PKT:
	case HCI_ACLDATA_PKT:
	case QPERI_ACLDATA_PKT:
		break;
	default:
		dev_kfree_skb_irq(skb);
		bt_dev_err_ratelimited(hdev, "unexpected pkt type 0x%02x", pkt_type);
		return -EINVAL;
	}

	return btusb_upward_frame(hdev, skb);
}

/*
 * RX reassembly state, stashed in hci_skb_pkt_seqnum(skb) between calls:
 * QRX_STATE_INDICATOR - determining the packet type
 * QRX_STATE_HEADER    - receiving the packet header
 * QRX_STATE_PAYLOAD  - receiving the payload
 */
enum {
	QRX_STATE_INDICATOR,
	QRX_STATE_HEADER,
	QRX_STATE_PAYLOAD,
};

/*
 * btusb_qcom_recv_intr - reassemble byte stream from the intr endpoint to frame
 * @hdev: the HCI device
 * @skb: the in-progress frame, or NULL to start a new one
 * @buffer: bytes received
 * @count: bytes count
 * @err: output error code
 *
 * See the RX flow diagram above for further dispatch.
 *
 * Return: the in-progress frame to resume on the next call, or NULL
 *	otherwise.
 */
struct sk_buff *btusb_qcom_recv_intr(struct hci_dev *hdev, struct sk_buff *skb,
				     void *buffer, int count, int *err)
{
	struct btqcom_data *qbt_data = hci_get_priv(hdev);
	enum qbtc_category btc_cat = qbt_data->category;
	int rx_state;
	u8 pkt_type;
	int len;
	int res;

	*err = 0;
	while (count) {
		if (!skb) {
			u8 host_id = *(u8 *)buffer;

			skb = bt_skb_alloc(QPERI_MAX_EVENT_SIZE, GFP_ATOMIC);
			if (!skb) {
				*err = -ENOMEM;
				break;
			}

			/* see struct qperi_event_hdr for PERI event format */
			if (host_id == QHCI_HOST_ID_BT) {
				hci_skb_pkt_type(skb) = QPERI_EVENT_PKT;
				hci_skb_pkt_seqnum(skb) = QRX_STATE_HEADER;
				hci_skb_expect(skb) = QPERI_EVENT_HDR_SIZE;
			} else {
				hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
				hci_skb_pkt_seqnum(skb) = QRX_STATE_HEADER;
				hci_skb_expect(skb) = HCI_EVENT_HDR_SIZE;
			}
		}

		len = min_t(uint, hci_skb_expect(skb), count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;
		hci_skb_expect(skb) -= len;

		if (hci_skb_expect(skb))
			continue;

		rx_state = hci_skb_pkt_seqnum(skb);
		if (rx_state == QRX_STATE_PAYLOAD)
			goto frame_done;

		hci_skb_pkt_seqnum(skb) = QRX_STATE_PAYLOAD;
		pkt_type = hci_skb_pkt_type(skb);
		if (pkt_type == QPERI_EVENT_PKT)
			hci_skb_expect(skb) = qperi_event_header(skb)->plen;
		else if (pkt_type == HCI_EVENT_PKT)
			hci_skb_expect(skb) = hci_event_hdr(skb)->plen;

		if (hci_skb_expect(skb))
			continue;

frame_done:
		if (count && count < HCI_EVENT_HDR_SIZE) {
			bt_dev_warn(hdev,
				    "Unexpected continuation: %d bytes",
				    count);
			if (btc_cat != QBTC_CAT_MSUBSYS)
				count = 0;
		}

		hci_skb_pkt_seqnum(skb) = 0;
		res = btqcom_recv_frame(hdev, skb);
		if (res)
			bt_dev_err_ratelimited(hdev, "recv intr frame failed: %pe",
					       ERR_PTR(res));
		skb = NULL;
	}

	return skb;
}

/*
 * btusb_qcom_recv_bulk - reassemble byte stream from the bulk endpoint to frame
 * @hdev: the HCI device
 * @skb: the in-progress frame, or NULL to start a new one
 * @buffer: bytes received
 * @count: bytes count
 * @err: output error code
 *
 * See the RX flow diagram above for further dispatch.
 *
 * Return: the in-progress frame to resume on the next call, or NULL
 *	otherwise.
 */
struct sk_buff *btusb_qcom_recv_bulk(struct hci_dev *hdev, struct sk_buff *skb,
				     void *buffer, int count, int *err)
{
	struct qperi_acl_hdr *peri_hdr;
	u16 peri_handle;
	int rx_state;
	u8 pkt_type;
	int res;
	int len;

	*err = 0;
	while (count) {
		if (!skb) {
			skb = bt_skb_alloc(QPERI_MAX_FRAME_SIZE, GFP_ATOMIC);
			if (!skb) {
				*err = -ENOMEM;
				break;
			}
			hci_skb_pkt_seqnum(skb) = QRX_STATE_INDICATOR;
			hci_skb_expect(skb) = HCI_ACL_HDR_SIZE;
		}

		len = min_t(uint, hci_skb_expect(skb), count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;
		hci_skb_expect(skb) -= len;

		if (hci_skb_expect(skb))
			continue;

		rx_state = hci_skb_pkt_seqnum(skb);
		switch (rx_state) {
		case QRX_STATE_INDICATOR:
			hci_skb_pkt_seqnum(skb) = QRX_STATE_HEADER;
			peri_hdr = qperi_acl_header(skb);
			peri_handle = qperi_acl_handle(skb);
			if (peri_hdr->host_id == QHCI_HOST_ID_BT &&
			    peri_handle >= 0xEC0 && peri_handle <= 0xECF) {
				hci_skb_pkt_type(skb) = QPERI_ACLDATA_PKT;
				hci_skb_expect(skb) = QPERI_ACL_HDR_SIZE - HCI_ACL_HDR_SIZE;
				continue;
			}
			hci_skb_pkt_type(skb) = HCI_ACLDATA_PKT;
			fallthrough;

		case QRX_STATE_HEADER:
			pkt_type = hci_skb_pkt_type(skb);
			if (pkt_type == QPERI_ACLDATA_PKT)
				hci_skb_expect(skb) = qperi_acl_dlen(skb);
			else if (pkt_type == HCI_ACLDATA_PKT)
				hci_skb_expect(skb) = hci_acl_dlen(skb);

			if (hci_skb_expect(skb) > QPERI_MAX_FRAME_SIZE - skb->len) {
				dev_kfree_skb_irq(skb);
				skb = NULL;
				*err = -EILSEQ;
				return NULL;
			}
			hci_skb_pkt_seqnum(skb) = QRX_STATE_PAYLOAD;
			if (hci_skb_expect(skb))
				continue;
			fallthrough;

		case QRX_STATE_PAYLOAD:
			break;
		}

		hci_skb_pkt_seqnum(skb) = 0;
		res = btqcom_recv_frame(hdev, skb);
		if (res)
			bt_dev_err_ratelimited(hdev, "recv bulk frame failed: %pe",
					       ERR_PTR(res));
		skb = NULL;
	}

	return skb;
}

MODULE_AUTHOR("Zijun Hu <zijun.hu@oss.qualcomm.com>");
