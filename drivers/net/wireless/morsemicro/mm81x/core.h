/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_CORE_H_
#define _MM81X_CORE_H_

#include <net/mac80211.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/crc32.h>
#include <linux/notifier.h>
#include <linux/nospec.h>
#include <linux/wait.h>
#include "yaps.h"
#include "yaps_hw.h"
#include "hw.h"
#include "fw.h"
#include "rc.h"

#define MM81X_DRIVER_SEMVER_MAJOR 56
#define MM81X_DRIVER_SEMVER_MINOR 3
#define MM81X_DRIVER_SEMVER_PATCH 0

#define MM81X_SEMVER_GET_MAJOR(x) (((x) >> 22) & 0x3FF)
#define MM81X_SEMVER_GET_MINOR(x) (((x) >> 10) & 0xFFF)
#define MM81X_SEMVER_GET_PATCH(x) ((x) & 0x3FF)

#define DRV_VERSION __stringify(MM81X_VERSION)

#define MM8108_FW_BASE "mm8108"

#define BCF_SIZE_MAX 48

#define KHZ100_TO_MHZ(x) ((x) / 10)
#define KHZ100_TO_KHZ(freq) ((freq) * 100)
#define KHZ100_TO_HZ(freq) ((freq) * 100000)

#define QDBM_TO_MBM(gain) (((gain) * 100) >> 2)
#define MBM_TO_QDBM(gain) (((gain) << 2) / 100)
#define QDBM_TO_DBM(gain) ((gain) / 4)

#define BPS_TO_KBPS(x) ((x) / 1000)

#define NSS_IDX_TO_NSS(x) ((x) + 1)
#define NSS_TO_NSS_IDX(x) ((x) - 1)

#define ROUND_BYTES_TO_WORD(_nbytes) \
	(((_nbytes) + 3) & ~((typeof(_nbytes))0x03))

struct mm81x_bus_ops;
struct mm81x_hif_ops;

#define MM81X_CAPS_MAX_FW_VAL (128)

/* Max number of interfaces */
#define MM81X_MAX_IF (2)

enum mm81x_caps_flags {
	MM81X_CAPS_FW_START = 0,
	MM81X_CAPS_2MHZ = MM81X_CAPS_FW_START,
	MM81X_CAPS_4MHZ,
	MM81X_CAPS_8MHZ,
	MM81X_CAPS_16MHZ,
	MM81X_CAPS_SGI,
	MM81X_CAPS_S1G_LONG,
	MM81X_CAPS_TRAVELING_PILOT_ONE_STREAM,
	MM81X_CAPS_TRAVELING_PILOT_TWO_STREAM,
	MM81X_CAPS_MU_BEAMFORMEE,
	MM81X_CAPS_MU_BEAMFORMER,
	MM81X_CAPS_RD_RESPONDER,
	MM81X_CAPS_STA_TYPE_SENSOR,
	MM81X_CAPS_STA_TYPE_NON_SENSOR,
	MM81X_CAPS_GROUP_AID,
	MM81X_CAPS_NON_TIM,
	MM81X_CAPS_TIM_ADE,
	MM81X_CAPS_BAT,
	MM81X_CAPS_DYNAMIC_AID,
	MM81X_CAPS_UPLINK_SYNC,
	MM81X_CAPS_FLOW_CONTROL,
	MM81X_CAPS_AMPDU,
	MM81X_CAPS_AMSDU,
	MM81X_CAPS_1MHZ_CONTROL_RESPONSE_PREAMBLE,
	MM81X_CAPS_PAGE_SLICING,
	MM81X_CAPS_RAW,
	MM81X_CAPS_MCS8,
	MM81X_CAPS_MCS9,
	MM81X_CAPS_ASYMMETRIC_BA_SUPPORT,
	MM81X_CAPS_DAC,
	MM81X_CAPS_CAC,
	MM81X_CAPS_TXOP_SHARING_IMPLICIT_ACK,
	MM81X_CAPS_NDP_PSPOLL,
	MM81X_CAPS_FRAGMENT_BA,
	MM81X_CAPS_OBSS_MITIGATION,
	MM81X_CAPS_TMP_PS_MODE_SWITCH,
	MM81X_CAPS_SECTOR_TRAINING,
	MM81X_CAPS_UNSOLICIT_DYNAMIC_AID,
	MM81X_CAPS_NDP_BEAMFORMING_REPORT,
	MM81X_CAPS_MCS_NEGOTIATION,
	MM81X_CAPS_DUPLICATE_1MHZ,
	MM81X_CAPS_TACK_AS_PSPOLL,
	MM81X_CAPS_PV1,
	MM81X_CAPS_TWT_RESPONDER,
	MM81X_CAPS_TWT_REQUESTER,
	MM81X_CAPS_BDT,
	MM81X_CAPS_TWT_GROUPING,
	MM81X_CAPS_LINK_ADAPTATION_WO_NDP_CMAC,
	MM81X_CAPS_LONG_MPDU,
	MM81X_CAPS_TXOP_SECTORIZATION,
	MM81X_CAPS_GROUP_SECTORIZATION,
	MM81X_CAPS_HTC_VHT,
	MM81X_CAPS_HTC_VHT_MFB,
	MM81X_CAPS_HTC_VHT_MRQ,
	MM81X_CAPS_2SS,
	MM81X_CAPS_3SS,
	MM81X_CAPS_4SS,
	MM81X_CAPS_SU_BEAMFORMEE,
	MM81X_CAPS_SU_BEAMFORMER,
	MM81X_CAPS_RX_STBC,
	MM81X_CAPS_TX_STBC,
	MM81X_CAPS_RX_LDPC,
	MM81X_CAPS_HW_FRAGMENT,

	MM81X_CAPS_FW_END = MM81X_CAPS_MAX_FW_VAL,
	MM81X_CAPS_LAST = MM81X_CAPS_FW_END,
};

struct mm81x_fw_caps {
	u32 flags[FW_CAPABILITIES_FLAGS_WIDTH];
	u8 ampdu_mss;
	u8 beamformee_sts_capability;
	u8 number_sounding_dimensions;
	u8 maximum_ampdu_length_exponent;
	u8 mm81x_mmss_offset;
};

#define MM81X_FW_SUPP(MM81X_CAPS, CAPABILITY) \
	mm81x_caps_supported(MM81X_CAPS, MM81X_CAPS_##CAPABILITY)

static inline bool mm81x_caps_supported(struct mm81x_fw_caps *caps,
					enum mm81x_caps_flags flag)
{
	const unsigned long *flags_ptr = (unsigned long *)caps->flags;

	return test_bit(flag, flags_ptr);
}

struct mm81x_ps {
	u32 wakers;
	bool enable;
	bool suspended;
	/* PS state lock */
	struct mutex lock;
	struct delayed_work delayed_eval_work;
};

enum mm81x_page_aci {
	MM81X_ACI_BE = 0,
	MM81X_ACI_BK = 1,
	MM81X_ACI_VI = 2,
	MM81X_ACI_VO = 3,
};

enum mm81x_qos_tid_up_index {
	MM81X_QOS_TID_UP_BK = 1,
	MM81X_QOS_TID_UP_XX = 2,
	MM81X_QOS_TID_UP_BE = 0,
	MM81X_QOS_TID_UP_EE = 3,
	MM81X_QOS_TID_UP_CL = 4,
	MM81X_QOS_TID_UP_VI = 5,
	MM81X_QOS_TID_UP_VO = 6,
	MM81X_QOS_TID_UP_NC = 7,

	MM81X_QOS_TID_UP_LOWEST = MM81X_QOS_TID_UP_BK,
	MM81X_QOS_TID_UP_HIGHEST = MM81X_QOS_TID_UP_NC
};

struct mm81x_sw_version {
	u8 major;
	u8 minor;
	u8 patch;
};

struct mm81x_sta {
	const struct ieee80211_vif *vif;
	u8 addr[ETH_ALEN];
	enum ieee80211_sta_state state;
	bool tid_tx[IEEE80211_NUM_TIDS];
	bool tid_start_tx[IEEE80211_NUM_TIDS];
	u8 tid_params[IEEE80211_NUM_TIDS];
	int max_bw_mhz;
	struct mm81x_rc_sta rc;
	struct mmrc_rate last_sta_tx_rate;
	s16 avg_rssi;
	bool tx_ps_filter_en;
};

struct mm81x_vif {
	struct mm81x *mors;
	u16 id;

	union {
		struct {
			bool is_assoc;
		} sta;
		struct {
			u32 num_stas;
			struct work_struct beacon_work;
		} ap;
	} u;
};

struct mm81x_stale_tx_status {
	/* Stale Tx lock */
	spinlock_t lock;
	struct timer_list timer;
};

struct mcast_filter {
	u8 count;
	/*
	 * Integer representation of the last four bytes of a multicast MAC
	 * address. The first two bytes are always 0x0100 (IPv4) or 0x3333
	 * (IPv6).
	 */
	__le32 addr_list[];
};

enum mm81x_hw_scan_op {
	MM81X_HW_SCAN_OP_START,
	MM81X_HW_SCAN_OP_STOP,
};

struct mm81x_hw_scan_params {
	struct ieee80211_hw *hw;

	/* vif which initiated the scan */
	struct ieee80211_vif *vif;
	bool has_directed_ssid;
	u32 dwell_time_ms;
	u32 dwell_on_home_ms;
	enum mm81x_hw_scan_op operation;
	bool store;
	struct sk_buff *probe_req;
	u16 num_chans;
	u16 allocated_chans;

	struct {
		struct ieee80211_channel *channel;
		/* Index into @ref powers_qdbm for the power of this channel */
		u8 power_idx;
	} *channels;

	s32 *powers_qdbm;
	u8 n_powers;
};

enum mm81x_hw_scan_state {
	HW_SCAN_STATE_IDLE,
	HW_SCAN_STATE_RUNNING,
	HW_SCAN_STATE_ABORTING,
};

struct mm81x_hw_scan {
	enum mm81x_hw_scan_state state;
	struct completion scan_done;
	struct mm81x_hw_scan_params *params;
	struct delayed_work timeout;
	u32 home_dwell_ms;
};

enum mm81x_hif_event_flags {
	MM81X_HIF_EVT_RX_PEND,
	MM81X_HIF_EVT_PAGE_RETURN_PEND,
	MM81X_HIF_EVT_TX_COMMAND_PEND,
	MM81X_HIF_EVT_TX_BEACON_PEND,
	MM81X_HIF_EVT_TX_MGMT_PEND,
	MM81X_HIF_EVT_TX_DATA_PEND,
	MM81X_HIF_EVT_TX_PACKET_FREED_UP_PEND,
	MM81X_HIF_EVT_DATA_TRAFFIC_PAUSE_PEND,
	MM81X_HIF_EVT_DATA_TRAFFIC_RESUME_PEND,
	MM81X_HIF_EVT_UPDATE_HW_CLOCK_REFERENCE,
};

enum mm81x_state_flags {
	MM81X_STATE_CHIP_UNRESPONSIVE,
	MM81X_STATE_DATA_QS_STOPPED,
	MM81X_STATE_DATA_TX_STOPPED,
	MM81X_STATE_REGDOM_SET_BY_USER,
	MM81X_STATE_REGDOM_SET_BY_OTP,
	MM81X_STATE_RELOAD_FW_AFTER_START,
	MM81X_STATE_HOST_TO_CHIP_TX_BLOCKED,
	MM81X_STATE_HOST_TO_CHIP_CMD_BLOCKED,
};

#define MM81X_COUNTRY_LEN (3)
#define INVALID_VIF_INDEX 0xFF

struct mm81x {
	u32 chip_id;
	u32 host_table_ptr;

	/* Refer to @enum mm81x_bus_type */
	u32 bus_type;
	u32 bcf_address;

	/*
	 * Parsed from the release tag, which should be in the format
	 * 'rel_<major>_<minor>_<patch>'. If the tag is not in this format
	 * then corresponding version field will be 0.
	 */
	struct mm81x_sw_version sw_ver;
	u8 macaddr[ETH_ALEN];
	u8 country[MM81X_COUNTRY_LEN];

	/* Mask of type @enum host_table_firmware_flags */
	u32 fw_flags;
	u32 fw_major;
	struct mm81x_fw_caps fw_caps;
	bool started;
	bool chip_was_reset;
	struct wiphy *wiphy;
	struct mm81x_hw_scan hw_scan;
	struct ieee80211_hw *hw;
	struct device *dev;

	struct ieee80211_vif __rcu *vifs[MM81X_MAX_IF];

	/* @mm81x_state_flags */
	unsigned long state_flags;

	u16 cmd_seq;
	struct completion *cmd_comp;
	/* Serialises commands */
	struct mutex cmd_lock;

	/* Serialises command completion */
	struct mutex cmd_wait;

	const struct mm81x_regs *regs;

	struct {
		union {
			struct mm81x_yaps yaps;
		} u;
		const struct mm81x_hif_ops *ops;
		/* See @enum mm81x_hif_event_flags for values */
		unsigned long event_flags;
		bool validate_skb_checksum;
	} hif;

	struct workqueue_struct *chip_wq;
	struct work_struct hif_work;
	struct work_struct usb_irq_work;
	struct mm81x_stale_tx_status stale_status;
	bool config_ps;
	struct mm81x_ps ps;

	/* Tx power in mBm received from the FW before association */
	s32 tx_power_mbm;
	s32 tx_max_power_mbm;

	const struct mm81x_bus_ops *bus_ops;
	struct mm81x_rc mrc;
	int rts_threshold;
	struct workqueue_struct *net_wq;
	struct work_struct tx_stale_work;
	wait_queue_head_t tx_empty_waitq;

	struct cfg80211_chan_def chandef;
	struct mcast_filter *mcast_filter;
	atomic_t num_bcn_vifs;
	unsigned long beacon_irqs_enabled;
	u8 drv_priv[] __aligned(sizeof(void *));
};

/* Map from mac80211 queue to Morse ACI value for page metadata */
static inline u8 map_mac80211q_2_mm81x_aci(u16 mac80211queue)
{
	switch (mac80211queue) {
	case IEEE80211_AC_VO:
		return MM81X_ACI_VO;
	case IEEE80211_AC_VI:
		return MM81X_ACI_VI;
	case IEEE80211_AC_BK:
		return MM81X_ACI_BK;
	default:
		return MM81X_ACI_BE;
	}
}

static inline enum mm81x_page_aci
dot11_tid_to_ac(enum mm81x_qos_tid_up_index tid)
{
	switch (tid) {
	case MM81X_QOS_TID_UP_BK:
	case MM81X_QOS_TID_UP_XX:
		return MM81X_ACI_BK;
	case MM81X_QOS_TID_UP_CL:
	case MM81X_QOS_TID_UP_VI:
		return MM81X_ACI_VI;
	case MM81X_QOS_TID_UP_VO:
	case MM81X_QOS_TID_UP_NC:
		return MM81X_ACI_VO;
	case MM81X_QOS_TID_UP_BE:
	case MM81X_QOS_TID_UP_EE:
	default:
		return MM81X_ACI_BE;
	}
}

static inline bool mm81x_is_data_tx_allowed(struct mm81x *mors)
{
	return !test_bit(MM81X_STATE_DATA_TX_STOPPED, &mors->state_flags) &&
	       !test_bit(MM81X_HIF_EVT_DATA_TRAFFIC_PAUSE_PEND,
			 &mors->hif.event_flags);
}

static inline struct ieee80211_vif *
mm81x_vif_to_ieee80211_vif(struct mm81x_vif *mors_vif)
{
	return container_of((void *)mors_vif, struct ieee80211_vif, drv_priv);
}

static inline struct mm81x_vif *
ieee80211_vif_to_mors_vif(struct ieee80211_vif *vif)
{
	return (struct mm81x_vif *)vif->drv_priv;
}

static inline struct mm81x *mm81x_vif_to_mors(struct mm81x_vif *mors_vif)
{
	return mors_vif->mors;
}

static inline u32 mm81x_generate_cssid(const u8 *ssid, u8 len)
{
	return ~crc32(~0, ssid, len);
}

int mm81x_beacon_init(struct mm81x_vif *mors_vif);
void mm81x_beacon_finish(struct mm81x_vif *mors_vif);
void mm81x_beacon_irq_handle(struct mm81x *mors, u32 status);
char *mm81x_core_get_fw_path(u32 chip_id, u32 fw_ver);
struct mm81x *mm81x_core_alloc(size_t priv_size, struct device *dev);
int mm81x_core_init(struct mm81x *mors);
int mm81x_core_register(struct mm81x *mors);
void mm81x_core_unregister(struct mm81x *mors);
void mm81x_core_deinit(struct mm81x *mors);
void mm81x_core_free(struct mm81x *mors);

#endif /* !_MM81X_MM81X_H_ */
